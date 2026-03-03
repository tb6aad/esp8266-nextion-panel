/**
 * ============================================================
 *  main.cpp
 *  ESP8266 + Nextion NX4832T035_011 + Apple HomeKit
 *  Faz 2: Tam entegrasyon
 * ============================================================
 *
 *  Bağlantılar:
 *    Nextion TX  →  D6 (GPIO12)  [ESP RX]
 *    Nextion RX  →  D5 (GPIO14)  [ESP TX]
 *    D3 (GPIO0)  →  Fabrika sıfırlama (5 sn basılı tut)
 *
 *  Ekran bileşenleri:
 *    t0  Saat            (ESP günceller — NTP)
 *    t1  Başlık          (Sabit "HomeKit Panel")
 *    t2  WiFi durumu     (ESP günceller)
 *    t3  "Sicaklik-Nem"  (Sabit etiket)
 *    t4  Sıcaklık + Nem  (ESP günceller — simüle)
 *    t5  HomeKit durumu  (ESP günceller)
 *    b0–b4  Butonlar     (Nextion → "BTN1"…"BTN5")
 *
 *  Buton davranışı (çift yön):
 *    Nextion basışı → HomeKit karakteristik toggle → Nextion renk güncelle
 *    HomeKit App değişikliği → setter callback → Nextion renk güncelle
 * ============================================================
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <SoftwareSerial.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <DHT.h>
#include <arduino_homekit_server.h>
#include "config.h"

// ─── DHT11 ───────────────────────────────────────────────────────────────────
#define DHT_PIN  13   // D7 = GPIO13
DHT dht(DHT_PIN, DHT11);

// ─── HomeKit Dışa Aktarımları (my_accessory.c) ───────────────────────────────
extern "C" homekit_server_config_t config;
extern "C" homekit_characteristic_t name;
extern "C" homekit_characteristic_t sw1_on;
extern "C" homekit_characteristic_t sw2_on;
extern "C" homekit_characteristic_t sw3_on;
extern "C" homekit_characteristic_t sw4_on;
extern "C" homekit_characteristic_t sw5_on;
extern "C" void accessory_init();

// ─── Sabitler ────────────────────────────────────────────────────────────────
#define NUM_SWITCHES        5
#define STATE_FILE          "/states.dat"
#define HAP_PORT            5556

static const uint32_t WIFI_RETRY_MS       = 5000;
static const uint32_t HEALTH_CHECK_MS     = 60000;
static const uint8_t  HEALTH_MAX_FAILURES = 3;

static const unsigned long TIME_INTERVAL  = 1000;   // saat: her 1 sn
static const unsigned long TEMP_INTERVAL  = 5000;   // sıcaklık: her 5 sn

// ─── Switch Karakteristik Pointer Dizisi ─────────────────────────────────────
static homekit_characteristic_t* sw_chars[NUM_SWITCHES];

// ─── WiFi Değişkenleri ────────────────────────────────────────────────────────
static uint8_t  s_bssid[6]           = {0};
static int32_t  s_channel             = 0;
static bool     s_bssid_valid         = false;
static bool     wifi_was_disconnected = false;
static uint32_t last_wifi_check       = 0;

// ─── Sağlık Kontrolü ─────────────────────────────────────────────────────────
static uint32_t last_health_check  = 0;
static uint32_t last_hk_active_ms  = 0;
static uint8_t  health_fail_count  = 0;
static bool     hk_ready           = false;
static bool     hk_paired          = false;   // ilk bağlantı gerçekleşti mi

// ─── Fabrika Sıfırlama ───────────────────────────────────────────────────────
static uint32_t factory_press_start = 0;
static bool     factory_active      = false;
static bool     factory_last        = HIGH;

// ─── Zamanlama ───────────────────────────────────────────────────────────────
static unsigned long lastTimeTick = 0;
static unsigned long lastTempTick = 0;

// ─── Nextion SoftwareSerial  RX=D6(GPIO12)  TX=D5(GPIO14) ───────────────────
SoftwareSerial nextion(D6, D5);

// ─── NTP ─────────────────────────────────────────────────────────────────────
WiFiUDP   ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", UTC_OFFSET_SEC, 60000);

// ─── Nextion okuma tamponu ───────────────────────────────────────────────────
String nextionBuffer = "";


// ============================================================
//  Nextion — komut gönderme yardımcıları
// ============================================================
void nxSend(const String& cmd) {
    nextion.print(cmd);
    nextion.write(0xFF);
    nextion.write(0xFF);
    nextion.write(0xFF);
}

void nxSetTxt(const char* obj, const String& val) {
    nxSend(String(obj) + ".txt=\"" + val + "\"");
}

void nxSetColor(const char* obj, const char* attr, uint16_t color) {
    nxSend(String(obj) + "." + attr + "=" + String(color));
}


// ============================================================
//  Buton görünümünü HK karakteristik değerine göre güncelle
//  Açık → yeşil (2016)   Kapalı → koyu gri (1057)
// ============================================================
void updateButtonVisual(int idx) {
    bool state = sw_chars[idx]->value.bool_value;
    String btn = "b" + String(idx);
    nxSend(btn + ".bco=" + (state ? "2016" : "1057"));
    nxSend("ref " + btn);
}

// Tüm butonları güncelle (boot veya state yükleme sonrası)
void updateAllButtons() {
    for (int i = 0; i < NUM_SWITCHES; i++) {
        updateButtonVisual(i);
    }
}


// ============================================================
//  HomeKit setter callback — App → ESP → Nextion
//  my_accessory.c'deki setter'lar bu fonksiyonu çağırır.
// ============================================================
extern "C" void hk_on_switch_changed(int idx, bool val) {
    Serial.printf("[HK] Switch %d: %s\n", idx + 1, val ? "ON" : "OFF");
    updateButtonVisual(idx);
    // State kaydet (delay olmadan — LittleFS non-blocking değil ama kısa)
    if (LittleFS.begin()) {
        File f = LittleFS.open(STATE_FILE, "w");
        if (f) {
            uint8_t buf[NUM_SWITCHES];
            for (int i = 0; i < NUM_SWITCHES; i++) {
                buf[i] = (uint8_t)sw_chars[i]->value.bool_value;
            }
            f.write(buf, NUM_SWITCHES);
            f.close();
        }
        LittleFS.end();
    }
}


// ============================================================
//  LittleFS — state yükle / kaydet
// ============================================================
void loadStates() {
    if (!LittleFS.begin()) {
        Serial.println("[State] LittleFS baslatilamadi.");
        return;
    }
    File f = LittleFS.open(STATE_FILE, "r");
    if (!f) {
        Serial.println("[State] Kayitli state yok.");
        LittleFS.end();
        return;
    }
    uint8_t buf[NUM_SWITCHES];
    if (f.read(buf, NUM_SWITCHES) == NUM_SWITCHES) {
        for (int i = 0; i < NUM_SWITCHES; i++) {
            sw_chars[i]->value.bool_value = (buf[i] != 0);
        }
        Serial.printf("[State] Yuklendi: %d %d %d %d %d\n",
                      buf[0], buf[1], buf[2], buf[3], buf[4]);
    }
    f.close();
    LittleFS.end();
}

void saveStates() {
    if (!LittleFS.begin()) return;
    File f = LittleFS.open(STATE_FILE, "w");
    if (!f) { LittleFS.end(); return; }
    uint8_t buf[NUM_SWITCHES];
    for (int i = 0; i < NUM_SWITCHES; i++) {
        buf[i] = (uint8_t)sw_chars[i]->value.bool_value;
    }
    f.write(buf, NUM_SWITCHES);
    f.close();
    LittleFS.end();
    Serial.printf("[State] Kaydedildi: %d %d %d %d %d\n",
                  buf[0], buf[1], buf[2], buf[3], buf[4]);
}


// ============================================================
//  Fabrika sıfırlama
// ============================================================
void factory_reset() {
    Serial.println("[Reset] Fabrika sifirlamasi!");
    homekit_storage_reset();
    WiFi.disconnect(true);
    delay(300);
    ESP.restart();
}

void handleResetButton() {
    uint32_t now = millis();
    bool reading = digitalRead(PIN_FACTORY);
    if (reading == LOW && factory_last == HIGH) {
        factory_press_start = now;
        factory_active = true;
    } else if (reading == HIGH) {
        factory_active = false;
    }
    if (factory_active && (now - factory_press_start) >= 5000) {
        factory_reset();
    }
    factory_last = reading;
}


// ============================================================
//  WiFi
// ============================================================
bool wifi_find_24ghz() {
    Serial.printf("[WiFi] 2.4GHz taranıyor: %s\n", WIFI_SSID);
    int n = WiFi.scanNetworks(false, false);
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == WIFI_SSID && WiFi.channel(i) <= 13) {
            memcpy(s_bssid, WiFi.BSSID(i), 6);
            s_channel = WiFi.channel(i);
            s_bssid_valid = true;
            Serial.printf("[WiFi] Bulundu — Kanal: %d\n", s_channel);
            WiFi.scanDelete();
            return true;
        }
    }
    WiFi.scanDelete();
    Serial.println("[WiFi] 2.4GHz bulunamadi.");
    return false;
}

void wifi_connect() {
    if (!s_bssid_valid) wifi_find_24ghz();
    if (s_bssid_valid) {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD, s_channel, s_bssid);
    } else {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    uint8_t attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Baglandi. IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] Baglanamadi!");
    }
}

void handleWiFiResilience() {
    uint32_t now = millis();
    if (now - last_wifi_check < WIFI_RETRY_MS) return;
    last_wifi_check = now;

    if (WiFi.status() != WL_CONNECTED) {
        wifi_was_disconnected = true;
        if (s_bssid_valid) {
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD, s_channel, s_bssid);
        } else {
            WiFi.reconnect();
        }
    } else if (wifi_was_disconnected) {
        wifi_was_disconnected = false;
        Serial.println("[WiFi] Yeniden baglandi. Restart...");
        delay(500);
        ESP.restart();
    }
}


// ============================================================
//  HomeKit
// ============================================================
void setupHomeKit() {
    accessory_init();
    uint8_t mac[WL_MAC_ADDR_LENGTH];
    WiFi.macAddress(mac);
    int len = snprintf(NULL, 0, "%s_%02X%02X%02X",
                       name.value.string_value, mac[3], mac[4], mac[5]);
    char* name_val = (char*)malloc(len + 1);
    if (name_val) {
        snprintf(name_val, len + 1, "%s_%02X%02X%02X",
                 name.value.string_value, mac[3], mac[4], mac[5]);
        name.value.string_value = name_val;
    }
    arduino_homekit_setup(&config);
    hk_ready = true;
    Serial.println("[HomeKit] Sunucu baslatildi.");
}

void handleHealthCheck() {
    uint32_t now = millis();
    if (now - last_health_check < HEALTH_CHECK_MS) return;
    last_health_check = now;

    if (WiFi.status() != WL_CONNECTED) return;

    if (arduino_homekit_connected_clients_count() > 0) {
        last_hk_active_ms = now;
        health_fail_count = 0;
        return;
    }
    if ((now - last_hk_active_ms) < HEALTH_CHECK_MS) return;

    health_fail_count++;
    Serial.printf("[Health] HK erisim yok. (%d/%d)\n", health_fail_count, HEALTH_MAX_FAILURES);

    if (health_fail_count < HEALTH_MAX_FAILURES) {
        MDNS.begin("esp8266nxt");
        MDNS.addService("_hap", "_tcp", HAP_PORT);
        MDNS.update();
        Serial.println("[Health] mDNS yenilendi.");
    } else {
        Serial.println("[Health] Esik asildi. Restart...");
        delay(500);
        ESP.restart();
    }
}


// ============================================================
//  Nextion — Nextion'dan gelen komutu işle
// ============================================================
void processNextionCmd(const String& cmd) {
    Serial.println("Nextion < " + cmd);
    for (int i = 0; i < NUM_SWITCHES; i++) {
        if (cmd == ("BTN" + String(i + 1))) {
            // Toggle HK karakteristik
            bool newVal = !sw_chars[i]->value.bool_value;
            sw_chars[i]->value = HOMEKIT_BOOL(newVal);
            homekit_characteristic_notify(sw_chars[i], sw_chars[i]->value);
            // Nextion renk güncelle
            updateButtonVisual(i);
            // State kaydet
            saveStates();
            return;
        }
    }
}

void readNextion() {
    static unsigned long lastByteTime = 0;

    while (nextion.available()) {
        uint8_t c = nextion.read();
        lastByteTime = millis();

        if (c == 0xFF) {
            nextionBuffer.trim();
            if (nextionBuffer.length() >= 4) {
                processNextionCmd(nextionBuffer);
            }
            nextionBuffer = "";
        } else if (c >= 32 && c < 127) {
            nextionBuffer += (char)c;
        }
    }

    // prints komutu 0xFF göndermez; 80ms sessizlik sonrası işle
    if (nextionBuffer.length() >= 4 && millis() - lastByteTime > 80) {
        nextionBuffer.trim();
        processNextionCmd(nextionBuffer);
        nextionBuffer = "";
    }
}


// ============================================================
//  Ekran güncelleme fonksiyonları
// ============================================================
void updateTime() {
    if (WiFi.status() == WL_CONNECTED) {
        timeClient.update();
    }
    nxSetTxt("t0", timeClient.getFormattedTime());
}

void updateTemperature() {
    float hum  = dht.readHumidity();
    float temp = dht.readTemperature();

    if (isnan(hum) || isnan(temp)) {
        Serial.println("[DHT11] Okuma hatasi!");
        nxSetTxt("t4", "-- C  -- %");
        return;
    }

    temp += DHT_TEMP_OFFSET;
    hum  += DHT_HUM_OFFSET;

    Serial.printf("[DHT11] Sicaklik: %.1f C  Nem: %.1f %%\n", temp, hum);
    nxSetTxt("t4", String((int)temp) + "C " + String((int)hum) + "%");
}

void updateWifiStatus() {
    if (WiFi.status() == WL_CONNECTED) {
        nxSetTxt("t2", "WiFi: OK");
        nxSetColor("t2", "pco", 2016);
    } else {
        nxSetTxt("t2", "WiFi: --");
        nxSetColor("t2", "pco", 63488);
    }
}

void updateHomeKitStatus() {
    // Gerçek HK bağlantısı: en az 1 client aktif olduğunda paired kabul et
    if (hk_ready && WiFi.status() == WL_CONNECTED
        && arduino_homekit_connected_clients_count() > 0) {
        hk_paired = true;
    }

    if (hk_paired) {
        nxSetTxt("t5", "HK: OK");
        nxSetColor("t5", "pco", 2016);    // yeşil
    } else {
        nxSetTxt("t5", "HK: --");
        nxSetColor("t5", "pco", 63488);   // kırmızı
    }
}


// ============================================================
//  SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
    nextion.begin(9600);
    dht.begin();

    Serial.println("\n[Sistem] Baslatiliyor...");

    // Fabrika sıfırlama — boot sırasında basılıysa kontrol et
    pinMode(PIN_FACTORY, INPUT_PULLUP);
    factory_last = digitalRead(PIN_FACTORY);
    if (factory_last == LOW) {
        Serial.println("[Reset] Boot'ta basili! 5 sn tut...");
        uint32_t t = millis();
        while (digitalRead(PIN_FACTORY) == LOW) {
            if (millis() - t > 5000) factory_reset();
        }
    }

    // Switch karakteristik pointer dizisini doldur
    sw_chars[0] = &sw1_on;
    sw_chars[1] = &sw2_on;
    sw_chars[2] = &sw3_on;
    sw_chars[3] = &sw4_on;
    sw_chars[4] = &sw5_on;

    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.disconnect(false);

    wifi_connect();
    setupHomeKit();

    // Kaydedilmiş state'leri yükle (HK server başladıktan sonra)
    loadStates();

    if (WiFi.status() == WL_CONNECTED) {
        timeClient.begin();
        timeClient.update();
    }

    last_hk_active_ms = millis();

    // Nextion boot tamamlaması için bekle
    delay(2000);

    // İlk tam ekran güncellemesi
    updateWifiStatus();
    updateHomeKitStatus();
    updateTemperature();
    updateTime();
    updateAllButtons();   // Kaydedilmiş state'leri ekrana yansıt

    Serial.println("[Sistem] Hazir.");
}


// ============================================================
//  LOOP
// ============================================================
void loop() {
    arduino_homekit_loop();
    yield();

    readNextion();
    handleResetButton();
    handleWiFiResilience();
    handleHealthCheck();

    unsigned long now = millis();

    if (now - lastTimeTick >= TIME_INTERVAL) {
        lastTimeTick = now;
        updateTime();
        updateWifiStatus();
        updateHomeKitStatus();
    }

    if (now - lastTempTick >= TEMP_INTERVAL) {
        lastTempTick = now;
        updateTemperature();
    }
}

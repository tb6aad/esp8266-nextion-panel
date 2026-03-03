/**
 * ============================================================
 *  my_accessory.c
 *  ESP8266 + Nextion + Apple HomeKit — 5 Switch Bridge
 *
 *  5 switch: Işık 1, Işık 2, Işık 3, Işık 4, Aletler
 *
 *  Setter callback'ler: HomeKit App → ESP → Nextion renk güncelle
 *  NOT: .c olarak kalmalıdır (HomeKit makroları C99 gerektirir)
 * ============================================================
 */

#include <Arduino.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include "config.h"

// ─── Bridge Adı ──────────────────────────────────────────────────────────────
#define BRIDGE_NAME  "Nextion Panel"

homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, BRIDGE_NAME);

// ─── Forward declaration ──────────────────────────────────────────────────────
// Setter'ların sw_on değişkenlerine referans verebilmesi için
// extern bildirimi setter'lardan önce gelmelidir.
extern homekit_characteristic_t sw1_on;
extern homekit_characteristic_t sw2_on;
extern homekit_characteristic_t sw3_on;
extern homekit_characteristic_t sw4_on;
extern homekit_characteristic_t sw5_on;

// ─── Setter callback — HomeKit App değişikliği → Nextion ─────────────────────
// hk_on_switch_changed() main.cpp'de extern "C" olarak tanımlıdır.
extern void hk_on_switch_changed(int idx, bool val);

static void sw1_setter(homekit_value_t v) { sw1_on.value = v; hk_on_switch_changed(0, v.bool_value); }
static void sw2_setter(homekit_value_t v) { sw2_on.value = v; hk_on_switch_changed(1, v.bool_value); }
static void sw3_setter(homekit_value_t v) { sw3_on.value = v; hk_on_switch_changed(2, v.bool_value); }
static void sw4_setter(homekit_value_t v) { sw4_on.value = v; hk_on_switch_changed(3, v.bool_value); }
static void sw5_setter(homekit_value_t v) { sw5_on.value = v; hk_on_switch_changed(4, v.bool_value); }

// ─── 5 Switch Karakteristiği ─────────────────────────────────────────────────
homekit_characteristic_t sw1_on = HOMEKIT_CHARACTERISTIC_(ON, false, .setter = sw1_setter);
homekit_characteristic_t sw2_on = HOMEKIT_CHARACTERISTIC_(ON, false, .setter = sw2_setter);
homekit_characteristic_t sw3_on = HOMEKIT_CHARACTERISTIC_(ON, false, .setter = sw3_setter);
homekit_characteristic_t sw4_on = HOMEKIT_CHARACTERISTIC_(ON, false, .setter = sw4_setter);
homekit_characteristic_t sw5_on = HOMEKIT_CHARACTERISTIC_(ON, false, .setter = sw5_setter);

// ─── Prototip ────────────────────────────────────────────────────────────────
void accessory_identify(homekit_value_t _value);

// ─── Aksesuar Tanımı ─────────────────────────────────────────────────────────
homekit_accessory_t *accessories[] = {

  // ── Bridge ──────────────────────────────────────────────────────────────────
  HOMEKIT_ACCESSORY(
    .id       = 1,
    .category = homekit_accessory_category_bridge,
    .services = (homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION,
        .characteristics = (homekit_characteristic_t*[]) {
          &name,
          HOMEKIT_CHARACTERISTIC(MANUFACTURER,      "boycar"),
          HOMEKIT_CHARACTERISTIC(MODEL,             "ESP8266-Nextion"),
          HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER,     "NXT5-000"),
          HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "2.0"),
          HOMEKIT_CHARACTERISTIC(IDENTIFY, accessory_identify),
          NULL
        }
      ),
      NULL
    }
  ),

  // ── Işık 1 ──────────────────────────────────────────────────────────────────
  HOMEKIT_ACCESSORY(
    .id       = 2,
    .category = homekit_accessory_category_switch,
    .services = (homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME,              "Isik 1"),
          HOMEKIT_CHARACTERISTIC(MANUFACTURER,      "boycar"),
          HOMEKIT_CHARACTERISTIC(MODEL,             "ESP8266-Btn"),
          HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER,     "NXT5-SW1"),
          HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "2.0"),
          HOMEKIT_CHARACTERISTIC(IDENTIFY, accessory_identify),
          NULL
        }
      ),
      HOMEKIT_SERVICE(SWITCH, .primary = true,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME, "Isik 1"),
          &sw1_on,
          NULL
        }
      ),
      NULL
    }
  ),

  // ── Işık 2 ──────────────────────────────────────────────────────────────────
  HOMEKIT_ACCESSORY(
    .id       = 3,
    .category = homekit_accessory_category_switch,
    .services = (homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME,              "Isik 2"),
          HOMEKIT_CHARACTERISTIC(MANUFACTURER,      "boycar"),
          HOMEKIT_CHARACTERISTIC(MODEL,             "ESP8266-Btn"),
          HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER,     "NXT5-SW2"),
          HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "2.0"),
          HOMEKIT_CHARACTERISTIC(IDENTIFY, accessory_identify),
          NULL
        }
      ),
      HOMEKIT_SERVICE(SWITCH, .primary = true,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME, "Isik 2"),
          &sw2_on,
          NULL
        }
      ),
      NULL
    }
  ),

  // ── Işık 3 ──────────────────────────────────────────────────────────────────
  HOMEKIT_ACCESSORY(
    .id       = 4,
    .category = homekit_accessory_category_switch,
    .services = (homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME,              "Isik 3"),
          HOMEKIT_CHARACTERISTIC(MANUFACTURER,      "boycar"),
          HOMEKIT_CHARACTERISTIC(MODEL,             "ESP8266-Btn"),
          HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER,     "NXT5-SW3"),
          HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "2.0"),
          HOMEKIT_CHARACTERISTIC(IDENTIFY, accessory_identify),
          NULL
        }
      ),
      HOMEKIT_SERVICE(SWITCH, .primary = true,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME, "Isik 3"),
          &sw3_on,
          NULL
        }
      ),
      NULL
    }
  ),

  // ── Işık 4 ──────────────────────────────────────────────────────────────────
  HOMEKIT_ACCESSORY(
    .id       = 5,
    .category = homekit_accessory_category_switch,
    .services = (homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME,              "Isik 4"),
          HOMEKIT_CHARACTERISTIC(MANUFACTURER,      "boycar"),
          HOMEKIT_CHARACTERISTIC(MODEL,             "ESP8266-Btn"),
          HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER,     "NXT5-SW4"),
          HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "2.0"),
          HOMEKIT_CHARACTERISTIC(IDENTIFY, accessory_identify),
          NULL
        }
      ),
      HOMEKIT_SERVICE(SWITCH, .primary = true,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME, "Isik 4"),
          &sw4_on,
          NULL
        }
      ),
      NULL
    }
  ),

  // ── Aletler ─────────────────────────────────────────────────────────────────
  HOMEKIT_ACCESSORY(
    .id       = 6,
    .category = homekit_accessory_category_switch,
    .services = (homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME,              "Aletler"),
          HOMEKIT_CHARACTERISTIC(MANUFACTURER,      "boycar"),
          HOMEKIT_CHARACTERISTIC(MODEL,             "ESP8266-Btn"),
          HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER,     "NXT5-SW5"),
          HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "2.0"),
          HOMEKIT_CHARACTERISTIC(IDENTIFY, accessory_identify),
          NULL
        }
      ),
      HOMEKIT_SERVICE(SWITCH, .primary = true,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME, "Aletler"),
          &sw5_on,
          NULL
        }
      ),
      NULL
    }
  ),

  NULL
};

// ─── HomeKit Sunucu Yapılandırması ───────────────────────────────────────────
homekit_server_config_t config = {
  .accessories = accessories,
  .password    = HOMEKIT_PASSWORD,
  .setupId     = "NXT5"
};

// ─── Tanımlama ───────────────────────────────────────────────────────────────
void accessory_identify(homekit_value_t _value) {
  ets_printf("[HomeKit] Kimlik testi.\n");
}

// ─── GPIO Başlangıç ──────────────────────────────────────────────────────────
void accessory_init() {
  ets_printf("[GPIO] Nextion panel modu — fiziksel buton yok.\n");
}

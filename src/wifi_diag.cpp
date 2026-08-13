/*
 * Isolated Wi-Fi diagnostic.
 *
 *   pio run -e wifidiag -t upload && pio device monitor
 *
 * Builds ALONE (see build_src_filter in platformio.ini): no TFT, no touch, no
 * SD, no games. If the radio finds networks here but not in the app, the fault
 * is interference from the rest of the firmware. If it finds nothing here
 * either, the problem is RF or power, not our code.
 */
#ifdef CYD_WIFI_DIAG

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_system.h>

static void banner() {
    Serial.println();
    Serial.println("=========================================");
    Serial.println(" Board Wi-Fi diagnostic");
    Serial.println("=========================================");
    Serial.printf("chip     : %s rev%d, %d core(s)\n",
                  ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
    Serial.printf("cpu freq : %u MHz\n", (unsigned)getCpuFrequencyMhz());
    Serial.printf("free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
    Serial.printf("sdk      : %s\n", esp_get_idf_version());
}

static const char* authName(wifi_auth_mode_t a) {
    switch (a) {
        case WIFI_AUTH_OPEN:            return "open";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ent";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
        default:                        return "?";
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    banner();

    // Backlight on so the board is visibly alive, nothing else touched.
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    delay(200);

    Serial.printf("MAC      : %s\n", WiFi.macAddress().c_str());

    wifi_country_t country;
    if (esp_wifi_get_country(&country) == ESP_OK) {
        Serial.printf("country  : %.2s  channels %d..%d  max %d dBm\n",
                      country.cc, country.schan,
                      country.schan + country.nchan - 1, country.max_tx_power);
    }

    int8_t power = 0;
    if (esp_wifi_get_max_tx_power(&power) == ESP_OK) {
        Serial.printf("tx power : %.2f dBm\n", power * 0.25f);
    }
}

void loop() {
    static uint32_t pass = 0;
    ++pass;

    Serial.printf("\n--- scan #%u (blocking, hidden included) ---\n", (unsigned)pass);
    const uint32_t t0 = millis();

    WiFi.scanDelete();
    // Blocking scan, show_hidden = true, passive = false, 500ms per channel.
    const int16_t n = WiFi.scanNetworks(false, true, false, 500);
    const uint32_t took = millis() - t0;

    Serial.printf("result   : %d   (took %u ms)\n", (int)n, (unsigned)took);
    if (n == WIFI_SCAN_FAILED) {
        Serial.println("           -2 = WIFI_SCAN_FAILED (driver refused the scan)");
    } else if (n == 0) {
        Serial.println("           radio ran the scan but heard NOTHING.");
        Serial.println("           -> RF/antenna/power, not the scan API.");
    }

    for (int16_t i = 0; i < n; ++i) {
        Serial.printf("  %2d) ch%-3d %4d dBm  %-9s %s\n",
                      (int)i + 1, WiFi.channel(i), (int)WiFi.RSSI(i),
                      authName(WiFi.encryptionType(i)),
                      WiFi.SSID(i).length() ? WiFi.SSID(i).c_str() : "<hidden>");
    }

    Serial.printf("free heap: %u\n", (unsigned)ESP.getFreeHeap());
    delay(8000);
}

#endif  // CYD_WIFI_DIAG

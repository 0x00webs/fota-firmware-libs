// ── Library configuration ─────────────────────────────────────────────────────
// All FOTA settings live in FotaUserConfig.h (same directory as this file).
// FotaConfig.h picks it up automatically via __has_include, so both this
// sketch and the library (.cpp) see the same compile-time settings —
// no platformio.ini build_flags changes are needed.
//
// Copy FotaUserConfig.h.example → FotaUserConfig.h and fill in your values.
#include "FotaUserConfig.h"

#include <WiFi.h>
#include <FotaClient.h>

FotaClient fota;

static bool connectWiFi(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > timeoutMs) {
      return false;
    }
    delay(250);
  }
  return true;
}

void setup() {
  Serial.begin(115200);

  if (!connectWiFi(20000)) {
    Serial.println("WiFi connection timed out");
    return;
  }

  fota.begin();

  // Use your platform root CA PEM in production, never nullptr.
  fota.setCACert(nullptr);

  FotaResult result = fota.performUpdate();
  if (result == FotaResult::OK) {
    Serial.println("Update installed, rebooting...");
    delay(1000);
    ESP.restart();
  }

  if (result != FotaResult::NO_UPDATE) {
    Serial.printf("OTA failed (%s): %s\n", fotaResultStr(result), fota.lastError());
  }
}

void loop() {
  // Recommended pattern: poll at server-provided interval.
  static uint32_t lastCheckMs = 0;
  const uint32_t intervalMs = (fota.checkIntervalSecs() > 0)
                                  ? fota.checkIntervalSecs() * 1000UL
                                  : 24UL * 3600UL * 1000UL;

  if (millis() - lastCheckMs >= intervalMs) {
    lastCheckMs = millis();
    FotaResult result = fota.performUpdate();
    if (result == FotaResult::OK) {
      delay(1000);
      ESP.restart();
    }
  }

  fota.tick();
  delay(200);
}

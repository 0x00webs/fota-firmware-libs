/*
 * BasicUpdate.ino — NodeWave FOTA client example for ESP8266
 *
 * 1. Replace the placeholders below with your real values.
 * 2. Flash this sketch.
 * 3. The device will:
 *      - Connect to WiFi
 *      - Probe the backend's /health endpoint
 *      - Check for a firmware update at boot
 *      - Poll again at the server-supplied interval
 *        (fallback: 24 h if the server doesn't return check_interval_seconds)
 *
 * Dependencies:
 *   - FOTA-Client-ESP8266 (this library)
 *   - ArduinoJson >= 7.0   (auto-installed via library.properties)
 *
 * Tested with:
 *   ESP8266 Arduino core >= 3.1.0
 *   PlatformIO espressif8266 platform >= 4.x
 */

// ── Library configuration ─────────────────────────────────────────────────────
// All FOTA settings live in FotaUserConfig.h (same directory as this file).
// FotaConfig.h picks it up automatically via __has_include, so both this
// sketch and the library (.cpp) see the same compile-time settings —
// no platformio.ini build_flags changes are needed.
//
// Copy FotaUserConfig.h.example → FotaUserConfig.h and fill in your values.
#include "FotaUserConfig.h"

#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <FotaClient.h>

// ── WiFi credentials ──────────────────────────────────────────────────────────
static const char *WIFI_SSID     = "Redmi";
static const char *WIFI_PASSWORD = "qwerty123";

FotaClient fota;

// ── Lifecycle callback ────────────────────────────────────────────────────────
static void onOtaEvent(const char *stage, const char *version, const char *error)
{
	if (error && error[0] != '\0')
		Serial.printf("[OTA] %s v%s — ERROR: %s\n", stage, version, error);
	else if (strcmp(stage, "DOWNLOADING") == 0)
		Serial.printf("[OTA] New firmware v%s found! Downloading...\n", version);
	else
		Serial.printf("[OTA] %s v%s\n", stage, version);
}

// ── WiFi helper ───────────────────────────────────────────────────────────────
static bool connectWiFi(uint32_t timeoutMs)
{
	WiFi.mode(WIFI_STA);
	WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

	Serial.printf("Connecting to WiFi: %s", WIFI_SSID);
	const uint32_t start = millis();
	while (WiFi.status() != WL_CONNECTED)
	{
		if (millis() - start > timeoutMs)
		{
			Serial.println(" TIMEOUT");
			return false;
		}
		delay(250);
		Serial.print('.');
	}
	Serial.printf("\nConnected!  IP: %s  RSSI: %d dBm\n",
				  WiFi.localIP().toString().c_str(), WiFi.RSSI());
	return true;
}

// ── Server health check ───────────────────────────────────────────────────────
static void checkServerHealth()
{
	Serial.printf("Testing server: %s/health\n", FOTA_SERVER_URL);
	BearSSL::WiFiClientSecure client;
	client.setInsecure(); // health probe only — not security-critical
	HTTPClient http;
	if (http.begin(client, String(FOTA_SERVER_URL) + "/health"))
	{
		ESP.wdtFeed();
		int code = http.GET();
		code == 200
			? Serial.println("Health check OK")
			: Serial.printf("Health check HTTP %d\n", code);
		http.end();
	}
	else
	{
		Serial.println("Health check: HTTPClient.begin() failed");
	}
}

// ── setup ─────────────────────────────────────────────────────────────────────
void setup()
{
	Serial.begin(115200);
	delay(1500); // let the serial monitor connect before printing the banner

	Serial.println();
	Serial.println("╔══════════════════════════════════╗");
	Serial.printf( "║   Firmware  v%-6s  — RUNNING    ║\n", FOTA_CURRENT_VERSION);
	Serial.println("╚══════════════════════════════════╝");

	if (!connectWiFi(20000))
	{
		Serial.println("WiFi connection timed out — skipping OTA check");
		return;
	}

	checkServerHealth();

	fota.begin();
	fota.onEvent(onOtaEvent);

	// Set your root CA in production; nullptr = insecure (dev only).
	fota.setCACert(FOTA_SERVER_CA_CERT);

	Serial.println("Checking for firmware update...");
	FotaResult result = fota.performUpdate();

	switch (result)
	{
	case FotaResult::OK:
		Serial.println("Flashed successfully! Rebooting in 3 s...");
		delay(3000);
		ESP.restart();
		break;
	case FotaResult::NO_UPDATE:
		Serial.printf("Up to date (v%s). Polling every %u s.\n",
					  FOTA_CURRENT_VERSION,
					  fota.checkIntervalSecs() ? fota.checkIntervalSecs() : 24u * 3600u);
		break;
	default:
		Serial.printf("OTA failed [%s]: %s\n", fotaResultStr(result), fota.lastError());
		break;
	}
}

// ── loop ──────────────────────────────────────────────────────────────────────
static unsigned long lastCheckMs = 0;

void loop()
{
	const uint32_t intervalSecs = fota.checkIntervalSecs() > 0
									  ? fota.checkIntervalSecs()
									  : 24UL * 3600UL;

	if (millis() - lastCheckMs >= (unsigned long)intervalSecs * 1000UL)
	{
		lastCheckMs = millis();
		Serial.printf("Periodic OTA check (interval=%u s)...\n", intervalSecs);

		FotaResult result = fota.performUpdate();
		if (result == FotaResult::OK)
		{
			Serial.println("Flashed successfully! Rebooting in 3 s...");
			delay(3000);
			ESP.restart();
		}
		else if (result != FotaResult::NO_UPDATE)
		{
			Serial.printf("OTA failed [%s]: %s\n", fotaResultStr(result), fota.lastError());
		}
	}

	delay(200);
	yield(); // keep the software watchdog fed
}

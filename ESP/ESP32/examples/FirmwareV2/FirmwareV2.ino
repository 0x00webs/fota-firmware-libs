/*
 * FirmwareV2 — Test binary v2.0.0
 *
 * Upload the compiled .bin to the FOTA dashboard as firmware v2.0.0,
 * then create a campaign targeting your device.  The device running
 * FirmwareV1 will download this and reboot into it.
 *
 * Signature verification is DISABLED — quick test binary only.
 *
 * Board:  ESP32-S3 DevKitC-1
 */

// ── Library configuration ────────────────────────────────────────────────────
// All FOTA settings live in FotaUserConfig.h next to this file.
#include "FotaUserConfig.h"

// ── Includes ─────────────────────────────────────────────────────────────────
#include <FotaClient.h>
#include <HTTPClient.h>
#include <WiFi.h>

FotaClient fota;

size_t getArduinoLoopTaskStackSize()
{
	return 32 * 1024;
}

// ─────────────────────────────────────────────────────────────────────────────
void setup()
{
	Serial.begin(115200);

	// ── Signature verification ─────────────────────────────────────────────
	// fota.setVerifySignature(false);
	// fota.setAutoFetchPublicKey(false);

	log_i("╔══════════════════════════════════╗");
	log_i("║   Firmware  v%-6s  — RUNNING    ║", FOTA_CURRENT_VERSION);
	log_i("╚══════════════════════════════════╝");

	log_i("Connecting to WiFi: %s", WIFI_SSID);
	WiFi.begin(WIFI_SSID, WIFI_PASS);
	while (WiFi.status() != WL_CONNECTED)
		delay(500);
	log_i("Connected!  IP: %s  RSSI: %d dBm",
		  WiFi.localIP().toString().c_str(), WiFi.RSSI());

	log_i("Testing server: %s/health", FOTA_SERVER_URL);
	{
		HTTPClient http;
		http.begin(String(FOTA_SERVER_URL) + "/health");
		int code = http.GET();
		if (code == 200)
		{
			log_i("Health check OK");
		}
		else
		{
			log_w("Health check HTTP %d", code);
		}
		http.end();
	}

	fota.begin();
	fota.setCACert(nullptr);

	log_i("Checking for firmware update...");
	FotaResult result = fota.performUpdate();

	switch (result)
	{
	case FotaResult::OK:
		log_i("Flashed successfully! Rebooting in 3 s...");
		delay(3000);
		ESP.restart();
		break;
	case FotaResult::NO_UPDATE:
		log_i("Up to date (v%s). Polling every %u s.",
			  FOTA_CURRENT_VERSION, fota.checkIntervalSecs() ? fota.checkIntervalSecs() : 7u * 24u * 3600u);
		break;
	default:
		log_e("OTA failed: %s | %s", fotaResultStr(result), fota.lastError());
		break;
	}
}

static unsigned long lastCheckMs = 0;

void loop()
{
	const uint32_t intervalSecs = fota.checkIntervalSecs() > 0
									  ? fota.checkIntervalSecs()
									  : 7UL * 24UL * 3600UL;
	if (millis() - lastCheckMs >= (unsigned long)intervalSecs * 1000UL)
	{
		lastCheckMs = millis();
		log_i("Periodic OTA check (interval=%u s)...", intervalSecs);
		FotaResult r = fota.performUpdate();
		if (r == FotaResult::OK)
		{
			log_i("Rebooting...");
			delay(3000);
			ESP.restart();
		}
		else if (r != FotaResult::NO_UPDATE)
			log_e("OTA failed: %s | %s", fotaResultStr(r), fota.lastError());
	}
	delay(1000);
}

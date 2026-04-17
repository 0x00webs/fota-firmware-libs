#pragma once

/*
 * FsotaClient.h — NodeWave Filesystem OTA client for ESP32
 *
 * Handles LittleFS / SPIFFS / FATFS partition updates over HTTPS.
 *
 * Usage:
 *   FsotaClient fsota;
 *
 *   void setup() {
 *     fsota.setServerUrl(FOTA_SERVER_URL);
 *     fsota.setHardwareModel(FOTA_HARDWARE_MODEL);
 *     fsota.setCurrentFsVersion("1.0.0");    // your current littlefs.bin version
 *     fsota.setFsType("LITTLEFS");
 *     fsota.setAuthToken(FOTA_AUTH_TOKEN);
 *     fsota.setServerCACert(serverCert);     // PEM root CA cert (optional but recommended)
 *   }
 *
 *   void loop() {
 *     if (millis() - lastCheck > FSOTA_CHECK_INTERVAL_MS) {
 *       FsotaResult r = fsota.performUpdate();
 *       if (r == FsotaResult::OK) { ESP.restart(); }
 *       lastCheck = millis();
 *     }
 *   }
 */

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "FsotaConfig.h"
#include "FsotaTypes.h"

/**
 * @class FsotaClient
 * @brief Filesystem OTA client — fetches and flashes filesystem images from the FOTA platform.
 *
 * Works in tandem with FotaClient (firmware OTA). The two classes are independent
 * so you can run firmware OTA without filesystem OTA and vice-versa.
 */
class FsotaClient
{
public:
	FsotaClient();
	~FsotaClient();

	// ── Configuration ────────────────────────────────────────────────────────

	/**
	 * Set the base URL of the FOTA backend (no trailing slash).
	 * Defaults to FSOTA_SERVER_URL from FsotaConfig.h.
	 */
	void setServerUrl(const char *url);

	/** Hardware model — must match filesystem_images.hardware_model in the database. */
	void setHardwareModel(const char *model);

	/** Semver of the filesystem image currently on the device (e.g. "1.0.0"). */
	void setCurrentFsVersion(const char *version);

	/**
	 * Filesystem type: "LITTLEFS" (default), "SPIFFS", "FATFS", or "CUSTOM".
	 * Used as the fs_type query parameter in the OTA check request.
	 */
	void setFsType(const char *fsType);

	/**
	 * Bearer token for authenticating API requests.
	 * Use a device API key (fota_d_…) or operator JWT.
	 */
	void setAuthToken(const char *token);

	/**
	 * PEM-encoded root CA certificate for TLS peer verification.
	 * Pass nullptr (default) to skip verification — NOT recommended for production.
	 */
	void setServerCACert(const char *caCert);

	/**
	 * Optional label of the filesystem partition in the partition table.
	 * Defaults to FSOTA_PARTITION_LABEL ("spiffs" or "littlefs").
	 *
	 * If the partition cannot be found by label, FsotaClient falls back to
	 * searching by subtype (ESP_PARTITION_SUBTYPE_DATA_SPIFFS).
	 */
	void setPartitionLabel(const char *label);

	/** Register a lifecycle event callback (see FsotaEventCallback). */
	void onEvent(FsotaEventCallback cb) { _cb = cb; }

	// ── Core API ─────────────────────────────────────────────────────────────

	/**
	 * Query the server for a filesystem update and apply it if available.
	 *
	 * This call is blocking — it performs the full download, verification,
	 * partition erase, and write sequence. Call from loop() at your desired
	 * poll interval (see FSOTA_CHECK_INTERVAL_MS).
	 *
	 * After FsotaResult::OK, call ESP.restart() (or set FSOTA_REBOOT_ON_SUCCESS 1)
	 * so the device remounts the new filesystem image.
	 *
	 * @return FsotaResult::OK on success.
	 */
	FsotaResult performUpdate();

	/**
	 * Check for an update without applying it.
	 *
	 * @param[out] manifest  Populated with the server response on success.
	 * @return FsotaResult::OK when an update is available (manifest is valid),
	 *         FsotaResult::NO_UPDATE when no newer image exists, or an error code.
	 */
	FsotaResult checkForUpdate(FsotaManifest &manifest);

	/** Return the error description of the last failed operation. */
	const char *lastError() const { return _lastError; }

	/** Return the last FsotaResult code. */
	FsotaResult lastResult() const { return _lastResult; }

private:
	// ── Configuration storage ────────────────────────────────────────────────
	char _serverUrl[256];
	char _hardwareModel[64];
	char _currentFsVersion[32];
	char _fsType[16];
	char _authToken[512];
	char _partitionLabel[32];
	const char *_caCert; ///< Points to caller-owned PEM string; not copied

	// ── State ────────────────────────────────────────────────────────────────
	char _lastError[128];
	FsotaResult _lastResult;
	FsotaEventCallback _cb;

	// ── Internals ────────────────────────────────────────────────────────────
	WiFiClientSecure *_wifiClient;

	/** Allocate and configure a WiFiClientSecure (CA cert or insecure mode). */
	WiFiClientSecure *_createWifiClient();

	/** Release the WiFiClientSecure created by _createWifiClient(). */
	void _releaseWifiClient(WiFiClientSecure *client);

	/** Add Authorization header to the HTTP client. */
	void _addAuthHeader(HTTPClient &http);

	/** Emit a lifecycle event to the registered callback. */
	void _emit(const char *stage, const char *version, const char *error = "");

	/** Store an error message and set the lastResult code. */
	FsotaResult _setError(FsotaResult code, const char *msg);

	/**
	 * Stream the binary from url to the target partition, computing SHA-256 throughout.
	 * Verifies the digest against expectedHash when FSOTA_VERIFY_SHA256 is enabled.
	 *
	 * @param url           Signed download URL (may be a redirect to Supabase).
	 * @param partition     Target esp_partition_t* (already found and validated).
	 * @param expectedHash  64-hex-char SHA-256 string (or "" to skip).
	 * @param expectedSize  Expected byte count (0 to skip size check).
	 */
	FsotaResult _streamToPartition(
		const char *url,
		const esp_partition_t *partition,
		const char *expectedHash,
		uint32_t expectedSize);

	/** Find the filesystem partition by label, falling back to type/subtype search. */
	const esp_partition_t *_findPartition();
};

#pragma once

/**
 * @file FsotaClient.h
 * @brief ESP8266 client for filesystem-over-the-air updates (LittleFS / SPIFFS / FATFS).
 *
 * Uses BearSSL for TLS and the Arduino Updater library (U_FS flag) for flashing.
 *
 * Typical usage:
 *   #include "FotaUserConfig.h"  // defines FOTA_SERVER_URL, FOTA_AUTH_TOKEN, etc.
 *   #include <FsotaClient.h>
 *
 *   FsotaClient fsota;
 *   FsotaResult r = fsota.performUpdate();
 *   if (r == FsotaResult::OK) { ESP.restart(); }
 *
 * Differences from the ESP32 FsotaClient:
 *   - Uses BearSSL::WiFiClientSecure and ESP8266HTTPClient.
 *   - Flashes via Update.begin(size, U_FS) + Update.write() + Update.end().
 *   - No partition selection by label — the Updater automatically targets the
 *     filesystem partition defined in the firmware image's partition table.
 *   - mbedTLS SHA-256 not available; SHA-256 is computed with BearSSL's br_sha256.
 *   - No PSRAM; chunk buffer is small (FSOTA_STREAM_CHUNK_SIZE bytes on heap).
 */

#include "FotaConfig.h"
#include "FsotaConfig.h"
#include "FsotaTypes.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <ESP8266HTTPClient.h>

/**
 * @class FsotaClient
 * @brief Check, download and flash filesystem images from the NodeWave FOTA platform.
 */
class FsotaClient
{
public:
	/** @brief Construct with compile-time defaults from FotaConfig.h / FsotaConfig.h. */
	FsotaClient();

	/** @brief Destructor — frees BearSSL X509List objects if allocated. */
	~FsotaClient();

	// ── Configuration setters ────────────────────────────────────────────────

	/** @brief Override the FOTA backend base URL (no trailing slash). */
	void setServerUrl(const char *url);

	/** @brief Set the hardware model string sent to the server during version check. */
	void setHardwareModel(const char *model);

	/** @brief Set the filesystem image version currently installed. */
	void setCurrentFsVersion(const char *version);

	/** @brief Set the filesystem type ("LITTLEFS", "SPIFFS", "FATFS", "CUSTOM"). */
	void setFsType(const char *fsType);

	/** @brief Set the Bearer token used for API authorisation. */
	void setAuthToken(const char *token);

	/**
	 * @brief Set root CA certificate (PEM) for TLS server verification.
	 *
	 * Pass nullptr to disable certificate verification (setInsecure — dev only).
	 * The PEM string is NOT copied; must remain valid for the lifetime of this object.
	 */
	void setServerCACert(const char *pem);

	/** @brief Register an event callback for OTA lifecycle stages. */
	void onEvent(FsotaEventCallback cb) { _cb = cb; }

	// ── Core API ─────────────────────────────────────────────────────────────

	/**
	 * @brief Check the server for a newer filesystem image.
	 *
	 * On return, if FsotaResult::OK, manifest is populated.
	 * FsotaResult::NO_UPDATE means the device is already up to date.
	 *
	 * @param[out] manifest Populated with image metadata if an update exists.
	 * @return FsotaResult::OK, NO_UPDATE, or an ERR_* code.
	 */
	FsotaResult checkForUpdate(FsotaManifest &manifest);

	/**
	 * @brief Full update pipeline: check → download → verify → flash.
	 *
	 * Calls checkForUpdate(), downloads the image, verifies SHA-256 (if
	 * FSOTA_VERIFY_SHA256 == 1), and writes it via the Updater (U_FS mode).
	 * Does NOT call ESP.restart() — the caller should reboot when ready.
	 *
	 * @return FsotaResult::OK on success, NO_UPDATE if nothing to update,
	 *         or an ERR_* code on failure.
	 */
	FsotaResult performUpdate();

	// ── Diagnostics ──────────────────────────────────────────────────────────

	/** @brief Human-readable description of the last error. */
	const char *lastError() const { return _lastError; }

	/** @brief Result code from the last API call. */
	FsotaResult lastResult() const { return _lastResult; }

private:
	// Config
	char _serverUrl[256];
	char _hardwareModel[64];
	char _currentFsVersion[32];
	char _fsType[16];
	char _authToken[512];

	// TLS
	const char *_caCert;              ///< PEM pointer — not copied, must outlive this object
	BearSSL::X509List *_x509;         ///< Parsed for setCACert path
	BearSSL::WiFiClientSecure *_wc;   ///< Heap-allocated during a request, released after

	// State
	char _lastError[128];
	FsotaResult _lastResult;
	FsotaEventCallback _cb;

	// Private helpers
	BearSSL::WiFiClientSecure *_createWifiClient();
	void _releaseWifiClient(BearSSL::WiFiClientSecure *client);
	void _addAuthHeader(ESP8266HTTPClient &http);
	void _emit(const char *stage, const char *version = "", const char *error = nullptr);
	FsotaResult _setError(FsotaResult code, const char *msg);
	FsotaResult _streamAndFlash(const char *url,
								const char *expectedHash,
								uint32_t expectedSize);
};

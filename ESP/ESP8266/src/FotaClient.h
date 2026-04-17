#pragma once

/**
 * @file FotaClient.h
 * @brief Main ESP8266 Arduino client for secure firmware-over-the-air updates.
 *
 * Uses BearSSL (via ESP8266WiFi) for TLS and the Updater library for OTA flash.
 *
 * Usage (minimal):
 *   #define FOTA_SERVER_URL      "https://api.example.com"
 *   #define FOTA_HARDWARE_MODEL  "ESP8266"
 *   #define FOTA_CURRENT_VERSION "1.0.0"
 *   #define FOTA_AUTH_TOKEN      "<operator bearer token>"
 *   #define FOTA_SIGNING_PUBLIC_KEY "-----BEGIN PUBLIC KEY-----\n..."
 *   #include <FotaClient.h>
 *
 *   FotaClient fota;
 *   fota.begin();
 *   FotaResult r = fota.performUpdate();
 *   if (r == FotaResult::OK) { ESP.restart(); }
 *
 * Differences from the ESP32 library:
 *   - Uses BearSSL::WiFiClientSecure and ESP8266HTTPClient.
 *   - Flashes via the Arduino Updater (Update.h) instead of esp_https_ota.
 *   - No rollback support (markHealthy / tick not available on ESP8266).
 *   - Ed25519 signature algorithm not supported; use ECDSA_P256.
 *   - Default FOTA_MAX_FIRMWARE_SIZE is 1 MiB (ESP8266 OTA slot size).
 */

#include "FotaConfig.h"
#include "FotaTypes.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <ESP8266HTTPClient.h>

/**
 * @class FotaClient
 * @brief High-level OTA client: secure check, download, verify, and flash pipeline for ESP8266.
 */
class FotaClient
{
public:
	/** @brief Construct a new FotaClient instance with compile-time defaults. */
	FotaClient();

	/** @brief Destructor — frees heap-allocated BearSSL certificate objects. */
	~FotaClient();

	/**
	 * @brief Initialize the client with values from FotaConfig.h.
	 * Call once in setup() after WiFi is connected.
	 *
	 * @param serverUrl       FOTA backend base URL (e.g. "https://api.example.com")
	 * @param hardwareModel   Hardware model identifier (e.g. "ESP8266")
	 * @param currentVersion  Currently running firmware version (e.g. "1.0.0")
	 * @param authToken       Bearer token used for OTA API authorization.
	 * @param deviceId        Device identifier for campaign-aware OTA targeting.
	 */
	void begin(const char *serverUrl = FOTA_SERVER_URL,
			   const char *hardwareModel = FOTA_HARDWARE_MODEL,
			   const char *currentVersion = FOTA_CURRENT_VERSION,
			   const char *authToken = FOTA_AUTH_TOKEN,
			   const char *deviceId = FOTA_DEVICE_ID);

	// ── Authentication ──────────────────────────────────────────────────────

	/** @brief Set the Bearer token sent in every API request. */
	void setAuthToken(const char *token);

	/**
	 * @brief Set the device business-key used for campaign-aware OTA resolution.
	 * When set, /ota/check includes ?device_id=<id>.
	 */
	void setDeviceId(const char *deviceId);

	// ── TLS (BearSSL) ────────────────────────────────────────────────────────

	/**
	 * @brief Set the root CA certificate (PEM) used to verify the FOTA server.
	 *
	 * Internally parsed into a BearSSL::X509List. The PEM string is copied,
	 * so the caller does not need to keep it alive after this call.
	 * Pass nullptr to fall back to setInsecure() (development only).
	 *
	 * @param pem NUL-terminated PEM certificate string, or nullptr.
	 */
	void setCACert(const char *pem);

	/**
	 * @brief Set the client certificate (PEM) for mutual TLS (mTLS).
	 *
	 * Must be paired with setClientKey(). Both RSA and EC client certificates
	 * are supported; the key type is detected automatically.
	 * The PEM string is copied internally.
	 *
	 * @param pem NUL-terminated PEM certificate string, or nullptr to clear.
	 */
	void setClientCert(const char *pem);

	/**
	 * @brief Set the client private key (PEM) for mutual TLS (mTLS).
	 * Must match the certificate set via setClientCert().
	 * The PEM string is copied internally.
	 *
	 * @param pem NUL-terminated PEM private key string, or nullptr to clear.
	 */
	void setClientKey(const char *pem);

	// ── Signature verification ───────────────────────────────────────────────

	/** @brief Set the PEM public key used to verify firmware signatures. */
	void setPublicKey(const char *pem);

	/**
	 * @brief Fetch the signing public key from the backend at runtime.
	 *
	 * Calls GET /api/v1/firmware/public-key and stores the returned PEM.
	 *
	 * @param expectedKeyId  Optional: 16-char hex key-id to pin against.
	 *                       Pass nullptr to skip key-id validation.
	 * @return FotaResult::OK on success, otherwise FotaResult::ERR_PUBKEY.
	 */
	FotaResult fetchPublicKey(const char *expectedKeyId = nullptr);

	// ── Core API ─────────────────────────────────────────────────────────────

	/**
	 * @brief Check whether a newer firmware is available.
	 *
	 * Calls GET /api/v1/ota/check?hardware=<model>&version=<current>.
	 *
	 * @param manifest  Filled with update details when an update is available.
	 * @return FotaResult::OK — update available; NO_UPDATE — already current;
	 *         ERR_* — error.
	 */
	FotaResult checkForUpdate(FotaManifest &manifest);

	/**
	 * @brief Run one-shot OTA flow: check + download + verify + flash.
	 *
	 * Steps:
	 *   1. checkForUpdate()         — GET /ota/check
	 *   2. fetchPublicKey()         — auto-fetch if FOTA_AUTO_FETCH_PUBLIC_KEY==1
	 *   3. Streaming download       — HTTP GET download_url, written via Update.h
	 *   4. fotaVerifySha256Digest() — integrity check against manifest.hash
	 *   5. fotaVerifySignature()    — ECDSA_P256 or RSA_SHA256 signature check
	 *   6. Update.end(true)         — commit flash and set boot partition
	 *
	 * Transient failures are retried up to FOTA_RETRY_COUNT times.
	 * Security failures (SHA-256, signature) are NEVER retried.
	 *
	 * @return FotaResult::OK on success (reboot required).
	 *         FotaResult::NO_UPDATE when already on the latest firmware.
	 *         FotaResult::ERR_* on failure.
	 */
	FotaResult performUpdate();

	// ── Runtime overrides ───────────────────────────────────────────────────

	/**
	 * @brief Register an OTA lifecycle event callback.
	 * Fired at: CHECKING, DOWNLOADING, VERIFYING, INSTALLING, COMPLETED, FAILED, NO_UPDATE.
	 * @param cb Callback function pointer, or nullptr to clear.
	 */
	void onEvent(FotaEventCallback cb);

	/** @brief Override maximum retry count (default: FOTA_RETRY_COUNT). */
	void setRetryCount(uint8_t count);

	/** @brief Override retry delay in milliseconds (default: FOTA_RETRY_DELAY_MS). */
	void setRetryDelay(uint32_t ms);

	// ── Diagnostics ─────────────────────────────────────────────────────────

	/** @brief Return the current OTA lifecycle state. */
	FotaState getState() const { return _state; }

	/** @brief Return accumulated diagnostic counters. */
	const FotaStats &getStats() const { return _stats; }

	/** @brief Reset all accumulated statistics to zero. */
	void resetStats();

	/**
	 * @brief Manually report OTA progress to the FOTA platform.
	 * Non-fatal; always returns FotaResult::OK on transport failure.
	 */
	FotaResult reportProgress(const char *targetVersion,
							  const char *status,
							  const char *errorMsg = nullptr);

	/** @brief Return the last error message string. */
	const char *lastError() const { return _lastError; }

	/**
	 * @brief Return server-suggested OTA check interval in seconds.
	 * Returns 0 before the first successful check.
	 */
	uint32_t checkIntervalSecs() const { return _checkIntervalSecs; }

private:
	// ── Configuration storage ──────────────────────────────────────────────
	char _serverUrl[256];
	char _hardwareModel[64];
	char _currentVersion[32];
	char _authToken[512];
	char _publicKeyPem[2048];
	char _lastError[128];
	char _deviceId[64];
	uint32_t _checkIntervalSecs;

	// ── BearSSL cert objects (heap-allocated, managed by setCACert etc.) ────
	BearSSL::X509List  *_bearCACert;    ///< Trust anchor for server cert verification
	BearSSL::X509List  *_bearClientCert; ///< Client cert for mTLS (or nullptr)
	BearSSL::PrivateKey *_bearClientKey; ///< Client key for mTLS (or nullptr)

	// ── Runtime options ────────────────────────────────────────────────────
	FotaEventCallback _eventCb;
	uint8_t _retryCount;
	uint32_t _retryDelayMs;

	// ── State machine + diagnostics ────────────────────────────────────────
	FotaState _state;
	FotaStats _stats;

	// ── Internal helpers ───────────────────────────────────────────────────

	/** Core update logic (single attempt without retry wrapper). */
	FotaResult _doUpdate();

	/** Fire user callback and report progress to platform. */
	void _emit(const char *stage, const char *version, const char *error = nullptr);

	/**
	 * Streaming download: HTTP GET → Update.write() chunks (computing SHA-256
	 * incrementally), verify integrity + signature, then commit via Update.end().
	 */
	FotaResult _streamVerifyFlash(const FotaManifest &manifest);

	void _setError(const char *fmt, ...);
	bool _addAuthHeader(HTTPClient &http);

	/** Configure a BearSSL::WiFiClientSecure with the stored certificates. */
	void _configureClient(BearSSL::WiFiClientSecure &c) const;

	/** Authenticated HTTP GET; fills respBuf. Returns HTTP code or <0. */
	int _httpGet(const char *url, char *respBuf, size_t respBufLen);

	/** Authenticated HTTP POST with JSON body. Returns HTTP code or <0. */
	int _httpPost(const char *url, const char *jsonBody,
				  char *respBuf, size_t respBufLen);
};

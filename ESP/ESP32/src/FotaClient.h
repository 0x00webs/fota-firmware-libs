#pragma once

/**
 * @file FotaClient.h
 * @brief Main ESP32 Arduino client for secure firmware-over-the-air updates.
 *
 * This header exposes the public API used to check, verify, and install
 * firmware updates from an it FOTA-compatible backend.
 */

/*
 * FotaClient.h - Main FOTA client for ESP32 (Arduino framework)
 *
 * Usage (minimal):
 *   #define FOTA_SERVER_URL      "https://api.example.com"
 *   #define FOTA_HARDWARE_MODEL  "ESP32-WROOM-32"
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
 * v1.0.0 additions:
 *   - onEvent(cb)         register a lifecycle callback (progress + errors)
 *   - setRetryCount(n)    override FOTA_RETRY_COUNT at runtime
 *   - setRetryDelay(ms)   override FOTA_RETRY_DELAY_MS at runtime
 *   - FOTA_REBOOT_ON_SUCCESS / FOTA_AUTO_FETCH_PUBLIC_KEY / FOTA_LOG_LEVEL
 */

#include "FotaConfig.h"
#include "FotaTypes.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

/**
 * @class FotaClient
 * @brief High-level OTA client with secure download, verification, and install pipeline.
 */
class FotaClient
{
public:
	/** @brief Construct a new FotaClient instance. */
	FotaClient();

	/**
	 * @brief Enable or disable signature verification at runtime.
	 *
	 * By default the behaviour is governed by the FOTA_VERIFY_SIGNATURE macro
	 * (settable via FotaUserConfig.h).  Calling this allows sketches to override
	 * that default without needing build‑flag hacks.
	 */
	void setVerifySignature(bool enable);

	/**
	 * @brief Enable or disable automatic public‑key fetch at runtime.
	 *
	 * Ignored unless signature verification is enabled.  The default value is
	 * taken from FOTA_AUTO_FETCH_PUBLIC_KEY.
	 */
	void setAutoFetchPublicKey(bool enable);


	/**
	 * @brief Initialize the client with values from FotaConfig.h.
	 *
	 * Call this once in setup() after WiFi is connected.
	 *
	 * @param serverUrl       FOTA backend base URL (e.g. "https://api.example.com")
	 * @param hardwareModel   Hardware model identifier (e.g. "ESP32-WROOM-32")
	 * @param currentVersion  Currently running firmware version (e.g. "1.0.0")
	 * @param authToken       Bearer token used for OTA API authorization.
	 * @param deviceId        Device identifier used for campaign-aware targeting.
	 * @note Emits info/debug logs describing configuration and rollback state.
	 * @return void
	 */
	void begin(const char *serverUrl = FOTA_SERVER_URL,
			   const char *hardwareModel = FOTA_HARDWARE_MODEL,
			   const char *currentVersion = FOTA_CURRENT_VERSION,
			   const char *authToken = FOTA_AUTH_TOKEN,
			   const char *deviceId = FOTA_DEVICE_ID);

	// ── Authentication ──────────────────────────────────────────────────────

	/**
	 * @brief Set the Bearer token sent in every API request.
	 * @param token NUL-terminated bearer token string.
	 * @return void
	 */
	void setAuthToken(const char *token);
	/**
	 * @brief Set the device business-key used for campaign-aware OTA resolution.
	 *
	 * When set, /ota/check includes ?device_id=<id> so the server can
	 * prioritise firmware targeted by an active campaign over the global latest.
	 * @param deviceId NUL-terminated device identifier.
	 * @return void
	 */
	void setDeviceId(const char *deviceId);
	// ── TLS ─────────────────────────────────────────────────────────────────

	/**
	 * @brief Set the root CA certificate (PEM) used to verify the FOTA server.
	 * Pass nullptr to skip peer verification (insecure; development only).
	 * @param pem NUL-terminated PEM certificate string, or nullptr.
	 * @return void
	 */
	void setCACert(const char *pem);

	/**
	 * @brief Set the client certificate (PEM) presented during the mTLS handshake.
	 *
	 * Call this together with setClientKey() to enable mutual TLS.
	 * The PEM string is referenced by pointer — ensure it remains valid for
	 * the lifetime of the FotaClient instance (e.g. stored in a DevicePKI).
	 *
	 * @param pem  NUL-terminated PEM certificate string (or nullptr to clear).
	 * @return void
	 */
	void setClientCert(const char *pem);

	/**
	 * @brief Set the client private key (PEM) used for the mTLS handshake.
	 *
	 * Must match the certificate set via setClientCert().
	 * The PEM string is referenced by pointer — ensure it remains valid for
	 * the lifetime of the FotaClient instance.
	 *
	 * @param pem  NUL-terminated PEM private key string (or nullptr to clear).
	 * @return void
	 */
	void setClientKey(const char *pem);

	// ── Signature verification ───────────────────────────────────────────────

	/**
	 * @brief Set the PEM public key used to verify firmware signatures.
	 * @param pem NUL-terminated PEM public key string.
	 * @return void
	 */
	void setPublicKey(const char *pem);

	/**
	 * @brief Fetch the signing public key from the backend at runtime.
	 *
	* Calls GET /api/v1/firmware/public-key and stores the returned PEM.
	 * Requires verified TLS (setCACert must have been called with a valid cert).
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
	 * @note Emits debug logs for request URLs and HTTP response codes.
	 * @return FotaResult::OK          — update available, manifest populated.
	 *         FotaResult::NO_UPDATE   — device is already up to date.
	 *         FotaResult::ERR_*       — error (see fotaResultStr()).
	 */
	FotaResult checkForUpdate(FotaManifest &manifest);

	/**
	 * @brief Run one-shot OTA flow: check + download + verify + flash.
	 *
	 * Steps performed on each attempt:
	 *   1. checkForUpdate()      — GET /ota/check
	 *   2. fetchPublicKey()      — auto-fetch if FOTA_AUTO_FETCH_PUBLIC_KEY==1
	 *   3. Download firmware     — HTTP GET download_url into PSRAM/heap buffer
	 *   4. fotaVerifySha256()    — integrity check against manifest.hash
	 *   5. fotaVerifySignature() — authenticity check (FOTA_VERIFY_SIGNATURE)
	 *   6. esp_https_ota()       — write to OTA partition and validate
	 *
	 * Transient network failures are retried up to FOTA_RETRY_COUNT times
	 * (overridable at runtime via setRetryCount()). Security failures
	 * (SHA-256 mismatch, invalid signature) are never retried.
	 *
	 * When FOTA_REPORT_PROGRESS == 1 and FOTA_DEVICE_ID is set, progress is
	 * automatically reported to the platform at each stage. Any registered
	 * onEvent() callback is also fired at every lifecycle stage.
	 *
	 * When FOTA_REBOOT_ON_SUCCESS == 1, the device reboots automatically
	 * on FotaResult::OK; otherwise the caller must call ESP.restart().
	 * @note Logs lifecycle milestones with log_i, state/HTTP details with log_d,
	 *       transfer progress with log_v, and failures with log_e.
	 *
	 * @return FotaResult::OK on success.
	 *         FotaResult::NO_UPDATE when already on the latest firmware.
	 *         FotaResult::ERR_* on failure.
	 */
	FotaResult performUpdate();

	// ── Runtime overrides ───────────────────────────────────────────────────

	/**
	 * @brief Register an OTA lifecycle event callback.
	 *
	 * The callback is fired at every stage of performUpdate():
	 * CHECKING, DOWNLOADING, VERIFYING, INSTALLING, COMPLETED, FAILED, NO_UPDATE.
	 * See FotaEventCallback in FotaTypes.h for parameter details.
	 * @param cb Callback function pointer, or nullptr to clear.
	 * @return void
	 */
	void onEvent(FotaEventCallback cb);

	/**
	 * @brief Override the number of retries on transient failures at runtime.
	 * @param count Maximum number of retry attempts.
	 * @return void
	 */
	void setRetryCount(uint8_t count);

	/**
	 * @brief Override the delay between retry attempts at runtime.
	 * @param ms Delay between attempts in milliseconds.
	 * @return void
	 */
	void setRetryDelay(uint32_t ms);

#if FOTA_USE_SD_TEMP
	/**
	 * @brief Override the SD card temp file path at runtime.
	 * Overrides FOTA_SD_TEMP_PATH from FotaConfig.h.
	 *
	 * Must be called before performUpdate().
	 * The path must start with '/' and the SD filesystem must already be
	 * initialized (SD.begin() / SD_MMC.begin()) before performUpdate().
	 *
	 * @param path  Absolute path on the SD filesystem (e.g. "/ota_tmp.bin").
	 * @return void
	 */
	void setSDTempPath(const char *path);
#endif

	// ── Post-boot health watchdog ────────────────────────────────────────────

	/**
	 * @brief Confirm the freshly-booted firmware is healthy.
	 *
	 * Call this once your application has passed its own health checks
	 * (WiFi connected, sensors responding, server reachable, etc.).
	 * This calls esp_ota_mark_app_valid_cancel_rollback() and disarms the
	 * watchdog, so the device stays on the new firmware permanently.
	 *
	 * Only meaningful when FOTA_HEALTH_TIMEOUT_MS > 0 and the device has
	 * just booted a freshly-flashed OTA partition (isHealthPending() == true).
	 * Safe to call at any time - it is a no-op in all other situations.
	 * @return void
	 */
	void markHealthy();

	/**
	 * @brief Enforce the post-boot health watchdog deadline.
	 *
	 * Call this periodically from loop() when FOTA_HEALTH_TIMEOUT_MS > 0.
	 * If markHealthy() has not been called within FOTA_HEALTH_TIMEOUT_MS ms
	 * of begin(), this triggers esp_ota_mark_app_invalid_rollback_and_reboot()
	 * and the device reboots back into the previous firmware automatically.
	 *
	 * This is a no-op when FOTA_HEALTH_TIMEOUT_MS == 0 (default).
	 * @return void
	 */
	void tick();

	/**
	 * @brief Returns true if markHealthy() must still be called to confirm the
	 * current firmware. Only true when FOTA_HEALTH_TIMEOUT_MS > 0 and the
	 * device has just booted a freshly-flashed OTA partition.
	 * @return true when health confirmation is pending.
	 */
	bool isHealthPending() const
	{
		return _healthPending;
	}

	// ── State & diagnostics ─────────────────────────────────────────────────────

	/**
	 * @brief Return the current state of the OTA state machine.
	 *
	 * Returns one of the FotaState enum values. Transitions:
	 *   IDLE → CHECKING → DOWNLOADING → VERIFYING → INSTALLING → DONE
	 * Any stage can transition to FAILED on error.
	 *
	 * Thread-safety: safe to read from loop(); updated only inside performUpdate().
	 * @return Current FotaState value.
	 */
	FotaState getState() const
	{
		return _state;
	}

	/**
	 * @brief Returns true while an update is actively in progress.
	 *
	 * Equivalent to: state > IDLE && state < DONE.
	 * Useful to guard against calling performUpdate() re-entrantly.
	 * @return true when update workflow is currently active.
	 */
	bool isUpdating() const
	{
		return _state > FotaState::IDLE && _state < FotaState::DONE;
	}

	/**
	 * @brief Return accumulated diagnostic counters since boot or resetStats().
	 *
	 * Returns a const reference that remains valid for the lifetime of the
	 * FotaClient instance. Counters are updated at the end of each
	 * performUpdate() call.
	 * @return Constant reference to internal FotaStats counters.
	 */
	const FotaStats &getStats() const
	{
		return _stats;
	}

	/**
	 * @brief Reset all accumulated statistics to zero.
	 *
	 * Also resets lastResult to FotaResult::NO_UPDATE.
	 * Does NOT reset the current state machine state.
	 * @return void
	 */
	void resetStats();

	/**
	 * @brief Manually report OTA progress to the FOTA platform.
	 *
	 * Called automatically by performUpdate(); expose publicly so that
	 * advanced sketches doing step-by-step updates can also report progress.
	 *
	 * Silently noops when FOTA_REPORT_PROGRESS == 0 or FOTA_DEVICE_ID is empty.
	 * Progress failures are NEVER fatal: always returns FotaResult::OK.
	 *
	 * @param targetVersion  Firmware version being installed (e.g. "1.2.3").
	 * @param status         One of: "DOWNLOADING", "VERIFYING", "INSTALLING",
	 *                               "COMPLETED", "FAILED".
	 * @param errorMsg       Optional: human-readable error detail for FAILED.
	 * @note Emits debug logs for HTTP status and warning logs for non-fatal failures.
	 * @return FotaResult::OK when accepted for send; non-OK on transport issues.
	 */
	FotaResult reportProgress(const char *targetVersion,
							  const char *status,
							  const char *errorMsg = nullptr);

	/**
	 * @brief Return the last error message string.
	 * @return Pointer to a NUL-terminated internal error message buffer.
	 */
	const char *lastError() const
	{
		return _lastError;
	}

	/**
	 * @brief Return server-suggested OTA check interval in seconds.
	 *
	 * Populated after the first successful checkForUpdate() / performUpdate()
	 * call from the \c check_interval_seconds field in the OTA check response.
	 * Returns 0 before the first successful check (use your own default then).
	 *
	 * Typical usage in loop():
	 * @code
	 *   static uint32_t lastCheckMs = 0;
	 *   uint32_t intervalMs = fota.checkIntervalSecs()
	 *                           ? fota.checkIntervalSecs() * 1000UL
	 *                           : 24UL * 3600UL * 1000UL; // fallback: 1 day
	 *   if (millis() - lastCheckMs >= intervalMs) {
	 *     lastCheckMs = millis();
	 *     fota.performUpdate();
	 *   }
	 * @endcode
	 * @return Recommended check interval in seconds, or 0 if unknown.
	 */
	uint32_t checkIntervalSecs() const
	{
		return _checkIntervalSecs;
	}

private:
	// ── Configuration storage ──────────────────────────────────────────────
	char _serverUrl[256];
	char _hardwareModel[64];
	char _currentVersion[32];
	char _authToken[512];
	const char *_caCert;
	const char *_clientCert; ///< mTLS client certificate PEM (or nullptr)
	const char *_clientKey;	 ///< mTLS client private key PEM (or nullptr)
	char _publicKeyPem[2048];
	char _lastError[128];
	char _deviceId[64];
	uint32_t _checkIntervalSecs; ///< Server-supplied check interval (0 until first check)
#if FOTA_USE_SD_TEMP
	char _sdTempPath[64]; ///< Temp file path on SD card (FOTA_SD_TEMP_PATH)
#endif

	// ── Post-boot health watchdog ────────────────────────────────────────────
	bool _healthPending;		///< true when markHealthy() has not yet been called
	uint32_t _healthDeadlineMs; ///< millis() deadline for health confirmation (0 = inactive)

	// ── Runtime options (can override FotaConfig.h defaults) ──────────────
	FotaEventCallback _eventCb; ///< Lifecycle event callback (or nullptr)
	uint8_t _retryCount;		///< Max transient-failure retries
	uint32_t _retryDelayMs;		///< ms to wait between retries
	// These flags can be modified at runtime via setVerifySignature()/
	// setAutoFetchPublicKey(). They default to the values of the corresponding
	// FOTA_ macros in FotaConfig.h.
	bool _verifySignature;
	bool _autoFetchPublicKey;
	// ── State machine + diagnostics ───────────────────────────────────
	FotaState _state; ///< Current OTA lifecycle state
	FotaStats _stats; ///< Accumulated diagnostic counters

	// ── TLS connection pool ────────────────────────────────────────────────
	WiFiClientSecure *_dlClient; ///< Shared download client within performUpdate() (non-owning)

	// ── Internal helpers ───────────────────────────────────────────────────

	/** Core update logic (single attempt). Called by performUpdate(). */
	FotaResult _doUpdate();

	/**
	 * Fire the user event callback AND report progress to the platform.
	 * Consolidates both notification channels into one call.
	 */
	void _emit(const char *stage, const char *version, const char *error = nullptr);

	/**
	 * Single-pass streaming download: writes directly to the OTA partition
	 * while computing SHA-256 and optionally verifying the signature.
	 * Eliminates the full-firmware heap allocation entirely.
	 */
	FotaResult _streamVerifyFlash(const FotaManifest &manifest);

#if FOTA_USE_SD_TEMP
	/**
	 * Two-pass update using an SD card as intermediate storage:
	 *   1. Download firmware binary to FOTA_SD_TEMP_PATH on the SD filesystem.
	 *   2. Verify SHA-256 and signature from the downloaded file (BEFORE
	 *      touching the OTA partition — hash mismatch is caught early).
	 *   3. Flash the OTA partition by reading from the SD temp file.
	 *   4. Delete the temp file (always, on both success and failure).
	 *
	 * The SD filesystem must be initialized before calling performUpdate().
	 */
	FotaResult _sdVerifyFlash(const FotaManifest &manifest);
#endif
	void _setError(const char *fmt, ...);
	bool _addAuthHeader(HTTPClient &http);
	/** Apply stored TLS configuration (CA cert, mTLS) to a WiFiClientSecure. */
	void _configureWifiClient(WiFiClientSecure &c) const;

	// GET:  returns HTTP status code or <0 on transport error.
	int _httpGet(const char *url, char *respBuf, size_t respBufLen);
	// POST: sends jsonBody, optionally reads response. Returns HTTP code or <0.
	int _httpPost(const char *url, const char *jsonBody,
				  char *respBuf, size_t respBufLen);
};

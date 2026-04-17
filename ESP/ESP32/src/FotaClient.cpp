/**
 * @file FotaClient.cpp
 * @brief Secure ESP32 OTA client implementation for check, verify, and flash flow.
 */

/*
 * FotaClient.cpp - FOTA client implementation for ESP32 (Arduino / ESP-IDF)
 *
 * Dependencies (Arduino library manager):
 *   - ArduinoJson (Benoit Blanchon) >= 7.0
 *
 * Built-in (ESP-IDF / arduino-esp32):
 *   - WiFiClientSecure, HTTPClient
 *   - esp_https_ota
 *   - mbedTLS (via FotaVerify.cpp)
 */

#include "FotaClient.h"
#include "FotaVerify.h"

#if FOTA_USE_SD_TEMP
#include <SD.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <ArduinoJson.h>
#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

static const char *TAG = "FotaClient";

#define LOGV(fmt, ...) log_v("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGD(fmt, ...) log_d("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGI(fmt, ...) log_i("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGW(fmt, ...) log_w("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGE(fmt, ...) log_e("[%s] " fmt, TAG, ##__VA_ARGS__)

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / begin
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Construct a FotaClient instance with compile-time defaults. */
FotaClient::FotaClient()
	: _serverUrl{0},
	  _hardwareModel{0},
	  _currentVersion{0},
	  _authToken{0},
	  _caCert(FOTA_SERVER_CA_CERT),
	  _clientCert(nullptr),
	  _clientKey(nullptr),
	  _publicKeyPem{0},
	  _lastError{0},
	  _deviceId{0},
	  _checkIntervalSecs(0),
#if FOTA_USE_SD_TEMP
	  _sdTempPath{0},
#endif
	  _healthPending(false),
	  _healthDeadlineMs(0),
	  _eventCb(nullptr),
	  _retryCount(FOTA_RETRY_COUNT),
	  _retryDelayMs(FOTA_RETRY_DELAY_MS),
	  _state(FotaState::IDLE),
	  _stats(),
	  _dlClient(nullptr),
	  _verifySignature(FOTA_VERIFY_SIGNATURE),
	  _autoFetchPublicKey(FOTA_AUTO_FETCH_PUBLIC_KEY)
{
	_serverUrl[0] = '\0';
	_hardwareModel[0] = '\0';
	_currentVersion[0] = '\0';
	_authToken[0] = '\0';
	_publicKeyPem[0] = '\0';
	_lastError[0] = '\0';
	_deviceId[0] = '\0';
	_checkIntervalSecs = 0;
	memset(&_stats, 0, sizeof(_stats));
	_stats.lastResult = FotaResult::NO_UPDATE;
#if FOTA_USE_SD_TEMP
	strncpy(_sdTempPath, FOTA_SD_TEMP_PATH, sizeof(_sdTempPath) - 1);
#endif
}

/** @brief Initialize runtime configuration and rollback/health handling. */
void FotaClient::begin(const char *serverUrl,
					   const char *hardwareModel,
					   const char *currentVersion,
					   const char *authToken,
					   const char *deviceId)
{
	strncpy(_serverUrl, serverUrl, sizeof(_serverUrl) - 1);
	strncpy(_hardwareModel, hardwareModel, sizeof(_hardwareModel) - 1);
	strncpy(_currentVersion, currentVersion, sizeof(_currentVersion) - 1);

	// authToken and deviceId default values are evaluated at the call site
	// (in the sketch) where the user's #define macros are visible — unlike
	// reading FOTA_AUTH_TOKEN directly here which would see only FotaConfig.h
	if (authToken && authToken[0] != '\0')
		strncpy(_authToken, authToken, sizeof(_authToken) - 1);

	if (deviceId && deviceId[0] != '\0')
		strncpy(_deviceId, deviceId, sizeof(_deviceId) - 1);

	// Copy compile-time public key if provided
	if (FOTA_SIGNING_PUBLIC_KEY[0] != '\0')
	{
		strncpy(_publicKeyPem, FOTA_SIGNING_PUBLIC_KEY, sizeof(_publicKeyPem) - 1);
	}

	// Apply FOTA_LOG_LEVEL to the ESP-IDF log subsystem for this library
	{
		static const esp_log_level_t kLevelMap[] = {
			ESP_LOG_NONE,
			ESP_LOG_ERROR,
			ESP_LOG_WARN,
			ESP_LOG_INFO,
			ESP_LOG_DEBUG};
		constexpr int kMaxLevel = (int)(sizeof(kLevelMap) / sizeof(kLevelMap[0])) - 1;
		int lvl = FOTA_LOG_LEVEL;
		if (lvl < 0)
			lvl = 0;
		if (lvl > kMaxLevel)
			lvl = kMaxLevel;
		esp_log_level_set("FotaClient", kLevelMap[lvl]);
		esp_log_level_set("FotaVerify", kLevelMap[lvl]);
	}

	ESP_LOGI(TAG, "FotaClient v1.0.0 | server=%s hw=%s ver=%s",
			 _serverUrl, _hardwareModel, _currentVersion);

#if FOTA_ROLLBACK_ENABLED
	{
		const esp_partition_t *running = esp_ota_get_running_partition();
		esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
		if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK &&
			ota_state == ESP_OTA_IMG_PENDING_VERIFY)
		{
#if FOTA_HEALTH_TIMEOUT_MS > 0
			// Deferred validation: arm the health watchdog. markHealthy() must
			// be called within FOTA_HEALTH_TIMEOUT_MS ms; tick() enforces it.
			_healthDeadlineMs = millis() + FOTA_HEALTH_TIMEOUT_MS;
			_healthPending = true;
			LOGI("Health watchdog armed (%u ms) — call markHealthy() to confirm firmware",
				 (unsigned)FOTA_HEALTH_TIMEOUT_MS);
#else
			// Immediate partition validation (default behaviour).
			LOGI("OTA partition pending verify — marking as valid (cancelling rollback)");
			esp_ota_mark_app_valid_cancel_rollback();
#endif
		}
	}
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration setters
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Set bearer token used by authenticated backend requests. */
void FotaClient::setAuthToken(const char *token)
{
	strncpy(_authToken, token, sizeof(_authToken) - 1);
}

/** @brief Set device identifier used for campaign-aware OTA responses. */
void FotaClient::setDeviceId(const char *deviceId)
{
	strncpy(_deviceId, deviceId, sizeof(_deviceId) - 1);
}

/** @brief Set TLS root CA used for server certificate verification. */
void FotaClient::setCACert(const char *pem)
{
	_caCert = pem;
}

/** @brief Set client certificate used for mutual TLS requests. */
void FotaClient::setClientCert(const char *pem)
{
	_clientCert = pem;
}

/** @brief Set client private key used for mutual TLS requests. */
void FotaClient::setClientKey(const char *pem)
{
	_clientKey = pem;
}

/** @brief Set firmware-signing public key for signature verification. */
void FotaClient::setPublicKey(const char *pem)
{
	strncpy(_publicKeyPem, pem, sizeof(_publicKeyPem) - 1);
}

/** @brief Register lifecycle callback for OTA stages. */
void FotaClient::onEvent(FotaEventCallback cb)
{
	_eventCb = cb;
}

/** @brief Set transient retry count for performUpdate() attempts. */
void FotaClient::setRetryCount(uint8_t count)
{
	_retryCount = count;
}

/** @brief Set delay between transient retry attempts in milliseconds. */
void FotaClient::setRetryDelay(uint32_t ms)
{
	_retryDelayMs = ms;
}

#if FOTA_USE_SD_TEMP
/** @brief Set SD temporary path used by SD-buffered OTA mode. */
void FotaClient::setSDTempPath(const char *path)
{
	strncpy(_sdTempPath, path, sizeof(_sdTempPath) - 1);
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Post-boot health watchdog
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Confirm booted firmware health and cancel rollback state. */
void FotaClient::markHealthy()
{
	if (_healthPending)
	{
		esp_ota_mark_app_valid_cancel_rollback();
		_healthPending = false;
		_healthDeadlineMs = 0;
		LOGI("Firmware marked healthy — rollback cancelled");
	}
}

/** @brief Enforce health watchdog timeout when deferred validation is enabled. */
void FotaClient::tick()
{
#if FOTA_HEALTH_TIMEOUT_MS > 0
	if (_healthPending && millis() >= _healthDeadlineMs)
	{
		LOGE("Health watchdog expired — marking firmware invalid and triggering rollback");
		esp_ota_mark_app_invalid_rollback_and_reboot();
		// esp_ota_mark_app_invalid_rollback_and_reboot() does not return.
	}
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// _configureWifiClient — apply CA cert + optional mTLS to a WiFiClientSecure
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Apply CA and optional mTLS credentials to a WiFiClientSecure instance. */
void FotaClient::_configureWifiClient(WiFiClientSecure &c) const
{
	if (_caCert)
	{
		c.setCACert(_caCert);
	}
	else
	{
		c.setInsecure();
		LOGW("TLS peer verification disabled — use setCACert() in production");
	}
	if (_clientCert)
		c.setCertificate(_clientCert);
	if (_clientKey)
		c.setPrivateKey(_clientKey);
}

/** @brief Reset diagnostic counters collected by the OTA state machine. */
void FotaClient::resetStats()
{
	memset(&_stats, 0, sizeof(_stats));
	_stats.lastResult = FotaResult::NO_UPDATE;
}

// ─────────────────────────────────────────────────────────────────────────────
// _emit — fire the user callback AND report progress to the platform
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Emit lifecycle callback and best-effort platform progress update. */
void FotaClient::_emit(const char *stage, const char *version, const char *error)
{
	const char *err = error ? error : "";
	FotaState prevState = _state;

	// Update state machine from stage name
	if (strcmp(stage, "CHECKING") == 0)
		_state = FotaState::CHECKING;
	else if (strcmp(stage, "DOWNLOADING") == 0)
		_state = FotaState::DOWNLOADING;
	else if (strcmp(stage, "VERIFYING") == 0)
		_state = FotaState::VERIFYING;
	else if (strcmp(stage, "INSTALLING") == 0)
		_state = FotaState::INSTALLING;
	else if (strcmp(stage, "COMPLETED") == 0)
		_state = FotaState::DONE;
	else if (strcmp(stage, "FAILED") == 0)
		_state = FotaState::FAILED;
	else if (strcmp(stage, "NO_UPDATE") == 0)
		_state = FotaState::IDLE;

	if (prevState != _state)
		LOGD("State transition: %d -> %d at stage=%s version=%s", (int)prevState, (int)_state, stage, version);

	// 1. User-registered lifecycle callback
	if (_eventCb)
		_eventCb(stage, version, err);

	// 2. Platform progress report (skip non-update stages)
	if (strcmp(stage, "CHECKING") != 0 && strcmp(stage, "NO_UPDATE") != 0)
		reportProgress(version, stage, (err[0] != '\0') ? err : nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

void FotaClient::setVerifySignature(bool enable)
{
	_verifySignature = enable;
}

void FotaClient::setAutoFetchPublicKey(bool enable)
{
	_autoFetchPublicKey = enable;
}

/** @brief Format and store last internal error for diagnostics and callbacks. */
void FotaClient::_setError(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vsnprintf(_lastError, sizeof(_lastError), fmt, args);
	va_end(args);
	LOGE("%s", _lastError);
}

/** @brief Add Authorization header if auth token is configured. */
bool FotaClient::_addAuthHeader(HTTPClient &http)
{
	if (_authToken[0] == '\0')
	{
		_setError("Auth token not configured. Call setAuthToken() or define FOTA_AUTH_TOKEN.");
		return false;
	}
	char bearer[520];
	snprintf(bearer, sizeof(bearer), "Bearer %s", _authToken);
	http.addHeader("Authorization", bearer);
	return true;
}

/** @brief Internal authenticated HTTP GET helper for JSON API endpoints. */
int FotaClient::_httpGet(const char *url, char *respBuf, size_t respBufLen)
{
	LOGD("HTTP GET begin: %s", url);
	// Log all HTTP headers being set
	LOGD("HTTP Header: Authorization: Bearer <hidden>");
	LOGD("HTTP Header: Accept: application/json");
	LOGD("HTTP Header: User-Agent: %s", FOTA_USER_AGENT);
	// Heap-allocate to avoid burning ~4 KB of stack per call on top of
	// the ~20 KB that mbedTLS already needs for the TLS handshake.
	WiFiClientSecure *wifiClient = new WiFiClientSecure();
	if (_caCert)
	{
		wifiClient->setCACert(_caCert);
	}
	else
	{
		wifiClient->setInsecure(); // Skip TLS verification (dev/test only)
		LOGW("TLS peer verification disabled — use setCACert() in production");
	}
	// mTLS: present client certificate if provisioned
	if (_clientCert)
		wifiClient->setCertificate(_clientCert);
	if (_clientKey)
		wifiClient->setPrivateKey(_clientKey);

	HTTPClient http;
	if (!http.begin(*wifiClient, url))
	{
		_setError("HTTPClient.begin() failed for URL: %s", url);
		return -1;
	}

	if (!_addAuthHeader(http))
	{
		http.end();
		delete wifiClient;
		return -2;
	}
	http.addHeader("Accept", "application/json");
	http.addHeader("User-Agent", FOTA_USER_AGENT);
	http.setTimeout(FOTA_HTTP_TIMEOUT_MS);

	int httpCode = http.GET();
	LOGD("HTTP GET response code=%d url=%s", httpCode, url);
	if (httpCode <= 0)
	{
		_setError("HTTP GET error: %s", http.errorToString(httpCode).c_str());
		http.end();
		delete wifiClient;
		return httpCode;
	}

	if (httpCode == HTTP_CODE_OK)
	{
		String body = http.getString();
		strncpy(respBuf, body.c_str(), respBufLen - 1);
		respBuf[respBufLen - 1] = '\0';
	}

	http.end();
	delete wifiClient;
	return httpCode;
}

// ─────────────────────────────────────────────────────────────────────────────
// fetchPublicKey
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Fetch public signing key metadata from backend firmware endpoint. */
FotaResult FotaClient::fetchPublicKey(const char *expectedKeyId)
{
	char url[512];
	snprintf(url, sizeof(url), "%s%s/firmware/public-key", _serverUrl, FOTA_API_PREFIX);
	// Patch: always use /api/v1/firmware/public-key regardless of FOTA_API_PREFIX
	snprintf(url, sizeof(url), "%s/api/v1/firmware/public-key", _serverUrl);

	char respBuf[4096];
	int httpCode = _httpGet(url, respBuf, sizeof(respBuf));
	LOGI("Public key endpoint raw JSON: %s", respBuf);
	if (httpCode != HTTP_CODE_OK)
	{
		if (httpCode == -2)
			return FotaResult::ERR_TOKEN;
		_setError("fetchPublicKey: HTTP %d", httpCode);
		return FotaResult::ERR_HTTP;
	}

	// Parse JSON: { public_key_pem, key_id, algorithm }
	JsonDocument doc;
	DeserializationError err = deserializeJson(doc, respBuf);
	if (err)
	{
		_setError("fetchPublicKey: JSON parse error: %s", err.c_str());
		return FotaResult::ERR_JSON;
	}

	const char *pem = doc["public_key_pem"] | "";
	const char *key_id = doc["key_id"] | "";

	if (pem[0] == '\0')
	{
		_setError("fetchPublicKey: empty public_key_pem in response");
		return FotaResult::ERR_PUBKEY;
	}

	// Optional key-id pinning
	if (expectedKeyId != nullptr && strcmp(key_id, expectedKeyId) != 0)
	{
		_setError("fetchPublicKey: key_id mismatch (expected=%s got=%s)",
				  expectedKeyId, key_id);
		return FotaResult::ERR_PUBKEY;
	}

	strncpy(_publicKeyPem, pem, sizeof(_publicKeyPem) - 1);
	LOGI("Public key fetched, key_id=%s", key_id);
	return FotaResult::OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// checkForUpdate
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Query backend for update availability and fill firmware manifest. */
FotaResult FotaClient::checkForUpdate(FotaManifest &manifest)
{
	if (_authToken[0] == '\0')
	{
		return FotaResult::ERR_TOKEN;
	}

	char url[512];
	if (_deviceId[0] != '\0')
	{
		snprintf(url, sizeof(url),
				 "%s%s/ota/check?hardware=%s&version=%s&device_id=%s",
				 _serverUrl, FOTA_API_PREFIX, _hardwareModel, _currentVersion, _deviceId);
	}
	else
	{
		snprintf(url, sizeof(url),
				 "%s%s/ota/check?hardware=%s&version=%s",
				 _serverUrl, FOTA_API_PREFIX, _hardwareModel, _currentVersion);
	}

	LOGI("OTA check: %s", url);

	char respBuf[2048];
	int httpCode = _httpGet(url, respBuf, sizeof(respBuf));
	LOGI("Manifest raw JSON: %s", respBuf);

	if (httpCode == -2)
		return FotaResult::ERR_TOKEN;
	if (httpCode != HTTP_CODE_OK)
	{
		_setError("OTA check HTTP %d", httpCode);
		return FotaResult::ERR_HTTP;
	}

	JsonDocument doc;
	DeserializationError err = deserializeJson(doc, respBuf);
	if (err)
	{
		_setError("OTA check JSON parse: %s", err.c_str());
		return FotaResult::ERR_JSON;
	}

	manifest.update_available = doc["update_available"] | false;

	// Always capture the server-supplied poll interval regardless of update_available
	manifest.check_interval_secs = doc["check_interval_seconds"] | 0u;
	if (manifest.check_interval_secs > 0)
		_checkIntervalSecs = manifest.check_interval_secs;

	if (!manifest.update_available)
	{
		LOGI("No update available");
		return FotaResult::NO_UPDATE;
	}

	// Populate manifest from JSON fields
	strncpy(manifest.firmware_id, doc["firmware_id"] | "", sizeof(manifest.firmware_id) - 1);
	strncpy(manifest.version, doc["version"] | "", sizeof(manifest.version) - 1);
	strncpy(manifest.hardware_model, doc["hardware_model"] | "", sizeof(manifest.hardware_model) - 1);
	strncpy(manifest.hash, doc["hash"] | "", sizeof(manifest.hash) - 1);
	strncpy(manifest.hash_algorithm, doc["hash_algorithm"] | "", sizeof(manifest.hash_algorithm) - 1);
	strncpy(manifest.signature, doc["signature"] | "", sizeof(manifest.signature) - 1);
	strncpy(manifest.signature_algorithm, doc["signature_algorithm"] | "", sizeof(manifest.signature_algorithm) - 1);
	strncpy(manifest.public_key_id, doc["public_key_id"] | "", sizeof(manifest.public_key_id) - 1);
	strncpy(manifest.download_url, doc["download_url"] | "", sizeof(manifest.download_url) - 1);
	strncpy(manifest.changelog, doc["changelog"] | "", sizeof(manifest.changelog) - 1);
	strncpy(manifest.campaign_id, doc["campaign_id"] | "", sizeof(manifest.campaign_id) - 1);
	manifest.file_size = doc["file_size"] | 0u;
	manifest.expires_in = doc["expires_in"] | 300u;

	LOGI("Update available: v%s (fw_id=%s, size=%u, algo=%s)",
		 manifest.version, manifest.firmware_id,
		 manifest.file_size, manifest.signature_algorithm);

	return FotaResult::OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// _httpPost — internal JSON POST helper
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Internal authenticated HTTP POST helper for JSON API endpoints. */
int FotaClient::_httpPost(const char *url, const char *jsonBody,
						  char *respBuf, size_t respBufLen)
{
	LOGD("HTTP POST begin: %s", url);
	WiFiClientSecure *wifiClient = new WiFiClientSecure();
	if (_caCert)
		wifiClient->setCACert(_caCert);
	else
		wifiClient->setInsecure();
	// mTLS: present client certificate if provisioned
	if (_clientCert)
		wifiClient->setCertificate(_clientCert);
	if (_clientKey)
		wifiClient->setPrivateKey(_clientKey);

	HTTPClient http;
	if (!http.begin(*wifiClient, url))
	{
		_setError("HTTPClient.begin() failed for POST: %s", url);
		delete wifiClient;
		return -1;
	}

	if (!_addAuthHeader(http))
	{
		http.end();
		delete wifiClient;
		return -2;
	}
	http.addHeader("Content-Type", "application/json");
	http.addHeader("User-Agent", FOTA_USER_AGENT);
	http.setTimeout(FOTA_HTTP_TIMEOUT_MS);

	int httpCode = http.POST((uint8_t *)jsonBody, strlen(jsonBody));
	LOGD("HTTP POST response code=%d url=%s payload_len=%u", httpCode, url, (unsigned)strlen(jsonBody));
	if (httpCode <= 0)
	{
		_setError("HTTP POST error: %s", http.errorToString(httpCode).c_str());
		http.end();
		delete wifiClient;
		return httpCode;
	}

	if (respBuf && respBufLen > 0 && httpCode == HTTP_CODE_OK)
	{
		String body = http.getString();
		strncpy(respBuf, body.c_str(), respBufLen - 1);
		respBuf[respBufLen - 1] = '\0';
	}

	http.end();
	delete wifiClient;
	return httpCode;
}

// ─────────────────────────────────────────────────────────────────────────────
// reportProgress — POST /ota/device/progress (best-effort, never fatal)
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Send non-fatal progress updates for OTA lifecycle tracking. */
FotaResult FotaClient::reportProgress(const char *targetVersion,
									  const char *status,
									  const char *errorMsg)
{
#if FOTA_REPORT_PROGRESS
	if (_deviceId[0] == '\0')
		return FotaResult::OK; // No device_id configured — skip silently

	char url[512];
	snprintf(url, sizeof(url), "%s%s/ota/device/progress", _serverUrl, FOTA_API_PREFIX);

	// Build JSON body (sanitise error message to avoid injection)
	char body[512];
	if (errorMsg && errorMsg[0] != '\0')
	{
		// Escape double-quotes to keep the JSON valid
		char safeMsg[128] = {};
		size_t j = 0;
		for (const char *p = errorMsg; *p && j < sizeof(safeMsg) - 2; p++)
		{
			if (*p == '"' || *p == '\\')
				safeMsg[j++] = '\\';
			safeMsg[j++] = *p;
		}
		snprintf(body, sizeof(body),
				 "{\"device_id\":\"%s\",\"target_version\":\"%s\","
				 "\"status\":\"%s\",\"error_message\":\"%s\"}",
				 _deviceId, targetVersion, status, safeMsg);
	}
	else
	{
		snprintf(body, sizeof(body),
				 "{\"device_id\":\"%s\",\"target_version\":\"%s\",\"status\":\"%s\"}",
				 _deviceId, targetVersion, status);
	}

	int httpCode = _httpPost(url, body, nullptr, 0);
	if (httpCode != HTTP_CODE_OK && httpCode != HTTP_CODE_NO_CONTENT && httpCode != HTTP_CODE_CREATED)
	{
		// Non-fatal: log warning but do not abort the update
		LOGW("reportProgress HTTP %d for status=%s (non-fatal)", httpCode, status);
	}
	else
	{
		LOGI("Progress reported: v%s → %s", targetVersion, status);
	}
#else
	(void)targetVersion;
	(void)status;
	(void)errorMsg;
#endif
	return FotaResult::OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// _streamVerifyFlash — single-pass streaming download + SHA-256 + OTA write
// No full-firmware buffer needed; only a small chunk buffer is allocated.
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Stream firmware download, verify cryptographic integrity, then flash OTA slot. */
FotaResult FotaClient::_streamVerifyFlash(const FotaManifest &manifest)
{
	LOGI("Update download started for version=%s", manifest.version);
	// Reject if file size was reported and exceeds our limit
	// (actual size check happens below after download)

	const char *url = manifest.download_url;

	// Use the shared download client from performUpdate() to enable TLS
	// session resumption across retry attempts. Fall back to a locally
	// configured client when called standalone.
	WiFiClientSecure localClient;
	if (!_dlClient)
		_configureWifiClient(localClient);
	WiFiClientSecure &wifiClient = _dlClient ? *_dlClient : localClient;

	HTTPClient http;
	if (!http.begin(wifiClient, url))
	{
		_setError("Download begin() failed");
		return FotaResult::ERR_DOWNLOAD;
	}

	http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
	if (_authToken[0] != '\0')
	{
		char bearer[520];
		snprintf(bearer, sizeof(bearer), "Bearer %s", _authToken);
		http.addHeader("Authorization", bearer);
	}
	http.addHeader("Accept", "application/octet-stream");
	http.addHeader("User-Agent", FOTA_USER_AGENT);
	http.setTimeout((uint16_t)(FOTA_DOWNLOAD_TIMEOUT_MS > 65535u ? 65535u : FOTA_DOWNLOAD_TIMEOUT_MS));

	int httpCode = http.GET();
	if (httpCode != HTTP_CODE_OK)
	{
		String errBody = http.getString();
		if (httpCode == HTTP_CODE_FORBIDDEN || httpCode == 410)
			_setError("Download URL expired or forbidden (HTTP %d) — retrying with fresh manifest", httpCode);
		else
			_setError("Download HTTP %d: %.120s", httpCode, errBody.c_str());
		http.end();
		return (httpCode == HTTP_CODE_FORBIDDEN || httpCode == 410)
				   ? FotaResult::ERR_EXPIRED
				   : FotaResult::ERR_DOWNLOAD;
	}

	int contentLen = http.getSize();
	if (contentLen <= 0)
	{
		_setError("Download: no Content-Length header");
		http.end();
		return FotaResult::ERR_DOWNLOAD;
	}

	if ((size_t)contentLen > FOTA_MAX_FIRMWARE_SIZE)
	{
		_setError("Firmware too large: %d > %d bytes", contentLen, FOTA_MAX_FIRMWARE_SIZE);
		http.end();
		return FotaResult::ERR_SIZE;
	}

	// ── Begin OTA partition write ───────────────────────────────────────────
	const esp_partition_t *updatePart = esp_ota_get_next_update_partition(nullptr);
	if (!updatePart)
	{
		_setError("No OTA partition found");
		http.end();
		return FotaResult::ERR_FLASH;
	}

	esp_ota_handle_t otaHandle = 0;
	if (esp_ota_begin(updatePart, (size_t)contentLen, &otaHandle) != ESP_OK)
	{
		_setError("esp_ota_begin failed");
		http.end();
		return FotaResult::ERR_FLASH;
	}

	// ── Chunk buffer — small, stack-friendly ───────────────────────────────
	static const size_t CHUNK_SIZE = 4096;
	uint8_t *chunk = (uint8_t *)malloc(CHUNK_SIZE);
	if (!chunk)
	{
		_setError("chunk malloc failed");
		esp_ota_abort(otaHandle);
		http.end();
		return FotaResult::ERR_ALLOC;
	}

#if FOTA_VERIFY_SHA256 || FOTA_VERIFY_SIGNATURE
	mbedtls_sha256_context shaCtx;
	mbedtls_sha256_init(&shaCtx);
	mbedtls_sha256_starts(&shaCtx, 0);
#endif

	WiFiClient *stream = http.getStreamPtr();
	size_t bytesWritten = 0;
	unsigned long deadline = millis() + FOTA_DOWNLOAD_TIMEOUT_MS;

	while (bytesWritten < (size_t)contentLen && millis() < deadline)
	{
		int avail = stream->available();
		if (avail > 0)
		{
			size_t toRead = avail < (int)CHUNK_SIZE ? (size_t)avail : CHUNK_SIZE;
			size_t remaining = (size_t)contentLen - bytesWritten;
			if (toRead > remaining)
				toRead = remaining;

			int got = stream->read(chunk, toRead);
			if (got > 0)
			{
				LOGI("Downloading chunk: offset=%u, size=%d", (unsigned)bytesWritten, got);
				mbedtls_sha256_update(&shaCtx, chunk, got);
				if (esp_ota_write(otaHandle, chunk, got) != ESP_OK)
				{
					_setError("esp_ota_write failed at offset %u", (unsigned)bytesWritten);
					free(chunk);
					esp_ota_abort(otaHandle);
					http.end();
					return FotaResult::ERR_FLASH;
				}
				bytesWritten += got;
				if ((bytesWritten % (64 * 1024)) < (size_t)got || bytesWritten == (size_t)contentLen)
					LOGV("Download progress: %u/%u bytes", (unsigned)bytesWritten, (unsigned)contentLen);
			}
		}
		else
		{
			// IRAM-safe busy-wait to avoid running code from flash while cache is disabled
			volatile uint32_t wait = 1000;
			while (wait--)
			{
				__asm__ __volatile__("nop");
			}
		}
		// WARNING: Avoid calling yield(), delay(), or any function that may run from flash here!
		// If a watchdog must be fed, use an IRAM-safe ISR or hardware timer.
	}

	free(chunk);
	http.end();

	if (bytesWritten != (size_t)contentLen)
	{
		_setError("Download incomplete: %u/%u bytes", (unsigned)bytesWritten, (unsigned)contentLen);
		esp_ota_abort(otaHandle);
		return FotaResult::ERR_DOWNLOAD;
	}

	LOGI("Downloaded %u bytes", (unsigned)bytesWritten);

	// ── SHA-256 verification ───────────────────────────────────────────────
	_emit("VERIFYING", manifest.version);
#if FOTA_VERIFY_SHA256 || FOTA_VERIFY_SIGNATURE
	uint8_t hash32[32];
	mbedtls_sha256_finish(&shaCtx, hash32);
	mbedtls_sha256_free(&shaCtx);
#endif

#if FOTA_VERIFY_SHA256
	if (fotaVerifySha256Digest(hash32, manifest.hash) != 0)
	{
		_setError("SHA-256 mismatch for firmware v%s", manifest.version);
		esp_ota_abort(otaHandle);
		return FotaResult::ERR_SHA256;
	}
	LOGI("SHA-256 OK");
#endif

#if FOTA_VERIFY_SIGNATURE
	if (_publicKeyPem[0] == '\0')
	{
		_setError("No public key for signature verification");
		esp_ota_abort(otaHandle);
		return FotaResult::ERR_PUBKEY;
	}
	if (fotaVerifySignature(hash32, manifest.signature,
							_publicKeyPem, manifest.signature_algorithm) != 0)
	{
		_setError("Signature verification failed for firmware v%s", manifest.version);
		esp_ota_abort(otaHandle);
		return FotaResult::ERR_SIGNATURE;
	}
	LOGI("Signature OK");
#endif

	// ── Commit and set boot partition ─────────────────────────────────────
	_emit("INSTALLING", manifest.version);
	if (esp_ota_end(otaHandle) != ESP_OK)
	{
		_setError("esp_ota_end failed");
		return FotaResult::ERR_FLASH;
	}
	if (esp_ota_set_boot_partition(updatePart) != ESP_OK)
	{
		_setError("esp_ota_set_boot_partition failed");
		return FotaResult::ERR_FLASH;
	}

	LOGI("OTA flash committed for v%s", manifest.version);
	LOGI("Update install finished for version=%s", manifest.version);
	return FotaResult::OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// _flashFirmware removed — replaced by _streamVerifyFlash which downloads,
// verifies (SHA-256 + signature), and flashes in a single streaming pass.

// ─────────────────────────────────────────────────────────────────────────────
// _doUpdate — single attempt: check → auto-fetch-key → download → verify → flash
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Execute one update attempt without retry wrapper logic. */
FotaResult FotaClient::_doUpdate()
{
	LOGI("Update attempt started: current_version=%s", _currentVersion);
	// ── 1. Notify: checking ────────────────────────────────────────────────	_stats.checkCount++;
	_stats.lastCheckMs = (uint32_t)millis();
	_emit("CHECKING", _currentVersion);

	// ── 2. Check for available update ─────────────────────────────────────
	FotaManifest manifest = {};
	LOGI("Step 1: Checking for update...");
	FotaResult r = checkForUpdate(manifest);
	if (r == FotaResult::NO_UPDATE)
	{
		LOGI("Step 1 result: No update available.");
		_emit("NO_UPDATE", _currentVersion);
		return FotaResult::NO_UPDATE;
	}
	if (r != FotaResult::OK)
	{
		LOGE("Step 1 result: Error during manifest fetch or parse.");
		return r;
	}
	LOGI("Step 1 result: Manifest parsed. Version: %s, Firmware ID: %s, Signature: %s", manifest.version, manifest.firmware_id, manifest.signature);

	// Snapshot the time immediately after the manifest is received.
	// Used below to detect whether the signed download URL has expired
	// before we start the download (e.g. if key-fetch takes a long time).
	const uint32_t manifestFetchedAtMs = millis();

	// ── 3. Auto-fetch public key if not already configured ─────────────────
	if (_verifySignature)
	{
		LOGI("Step 2: Verifying/fetching public key...");
		if (_autoFetchPublicKey && _publicKeyPem[0] == '\0' && manifest.public_key_id[0] != '\0')
		{
			LOGI("Auto-fetching public key (runtime auto‑fetch enabled)");
			FotaResult pkr = fetchPublicKey(nullptr);
			if (pkr != FotaResult::OK)
			{
				LOGE("Step 2 result: Failed to fetch public key.");
				_emit("FAILED", manifest.version, _lastError);
				return FotaResult::ERR_PUBKEY;
			}
		}
		if (_publicKeyPem[0] == '\0')
		{
			LOGE("Step 2 result: No public key available after fetch attempt.");
			_setError("No public key. Call setPublicKey(), define FOTA_SIGNING_PUBLIC_KEY, "
					  "or enable FOTA_AUTO_FETCH_PUBLIC_KEY.");
			_emit("FAILED", manifest.version, _lastError);
			return FotaResult::ERR_PUBKEY;
		}
		LOGI("Step 2 result: Public key ready for signature verification.");
	}

	// ── Check download URL hasn't expired since manifest was received ───────
	{
		const uint32_t elapsedSec = (uint32_t)((millis() - manifestFetchedAtMs) / 1000UL);
		if (manifest.expires_in > 0 && elapsedSec + FOTA_URL_EXPIRY_MARGIN_S >= manifest.expires_in)
		{
			LOGE("Step 3: Download URL expired (age %us >= expires_in %us)", (unsigned)elapsedSec, (unsigned)manifest.expires_in);
			_setError("Download URL expired (age %us >= expires_in %us) — will re-fetch manifest",
					  (unsigned)elapsedSec, (unsigned)manifest.expires_in);
			_emit("FAILED", manifest.version, _lastError);
			return FotaResult::ERR_EXPIRED;
		}
		LOGI("Step 3: Download URL valid, proceeding to download and flash.");
	}

	// ── 4. Download → verify → flash ────────────────────────────────────────
	_emit("DOWNLOADING", manifest.version);
#if FOTA_USE_SD_TEMP
	LOGI("SD-buffered flash v%s (%u bytes) → %s",
		 manifest.version, (unsigned)manifest.file_size, _sdTempPath);
	r = _sdVerifyFlash(manifest);
#else
	// Peak extra RAM = one 4 KB chunk buffer; no full-firmware allocation.
	LOGI("Streaming flash v%s (%u bytes)...", manifest.version, (unsigned)manifest.file_size);
	r = _streamVerifyFlash(manifest);
#endif
	if (r != FotaResult::OK)
	{
		_emit("FAILED", manifest.version, _lastError);
		return r;
	}

	_emit("COMPLETED", manifest.version);
	LOGI("Firmware v%s flashed successfully.", manifest.version);
	return FotaResult::OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// performUpdate — retry wrapper around _doUpdate()
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Perform OTA workflow with retry and state/statistics accounting. */
FotaResult FotaClient::performUpdate()
{
	LOGI("performUpdate() started");
	const uint8_t maxAttempts = (_retryCount < 10 ? _retryCount : 10) + 1;
	FotaResult r = FotaResult::ERR_HTTP;

	// Allocate one WiFiClientSecure for the entire performUpdate() run.
	// Reusing the same TLS context lets mbedTLS resume the session (via TLS
	// session tickets), skipping the full ~1–2 s handshake on retry attempts
	// that hit the same download host (typical with Supabase signed URLs).
	WiFiClientSecure dlClient;
	_configureWifiClient(dlClient);
	_dlClient = &dlClient;

	for (uint8_t attempt = 0; attempt < maxAttempts; attempt++)
	{
		LOGD("performUpdate attempt %u/%u", (unsigned)(attempt + 1), (unsigned)maxAttempts);
		if (attempt > 0)
		{
			// Before sleeping the retry delay, wait for WiFi to come back if
			// it dropped during the last attempt. This avoids burning a retry
			// on a link that is guaranteed to fail immediately.
#if FOTA_WIFI_RECONNECT_TIMEOUT_MS > 0
			if (WiFi.status() != WL_CONNECTED)
			{
				LOGW("WiFi disconnected — waiting up to %u ms for reconnect",
					 (unsigned)FOTA_WIFI_RECONNECT_TIMEOUT_MS);
				const unsigned long wifiDeadline = millis() + FOTA_WIFI_RECONNECT_TIMEOUT_MS;
				while (WiFi.status() != WL_CONNECTED && millis() < wifiDeadline)
				{
					delay(500);
#if FOTA_WATCHDOG_FEED
					yield();
#endif
				}
				if (WiFi.status() != WL_CONNECTED)
				{
					LOGE("WiFi reconnect timed out — aborting retries");
					r = FotaResult::ERR_WIFI;
					break;
				}
				LOGI("WiFi reconnected");
			}
#endif
			LOGW("Retry %u/%u — waiting %u ms", attempt, _retryCount, _retryDelayMs);
			delay(_retryDelayMs);
		}

		r = _doUpdate();

		// Never retry these outcomes
		if (r == FotaResult::OK ||
			r == FotaResult::NO_UPDATE ||
			r == FotaResult::ERR_SHA256 ||
			r == FotaResult::ERR_SIGNATURE ||
			r == FotaResult::ERR_TOKEN ||
			r == FotaResult::ERR_PUBKEY ||
			r == FotaResult::ERR_FLASH ||
			r == FotaResult::ERR_SIZE)
			break;

		LOGW("Transient failure (%s), will retry", fotaResultStr(r));
	}

	_dlClient = nullptr; // release reference; dlClient destroyed at scope exit

	// Update diagnostic counters
	_stats.lastResult = r;
	if (r == FotaResult::OK)
	{
		_stats.updateCount++;
		_stats.lastUpdateMs = (uint32_t)millis();
	}
	else if (r != FotaResult::NO_UPDATE)
	{
		_stats.failCount++;
	}

#if FOTA_REBOOT_ON_SUCCESS
	if (r == FotaResult::OK)
	{
		LOGI("FOTA_REBOOT_ON_SUCCESS=1: restarting in 1 s");
		delay(1000);
		esp_restart();
	}
#else
	if (r == FotaResult::OK)
		LOGI("Flash done. Call ESP.restart() to boot the new firmware.");
#endif

	return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// _sdVerifyFlash — two-pass SD-buffered download + verify + flash
//
// Pass 1: HTTP GET → SD temp file  (SHA-256 computed while writing)
//         Range: header used on retries for partial-download resume.
// Pass 2: Verify SHA-256 + signature (before touching OTA partition)
//       → Read SD file → esp_ota_write() chunks → commit
// Cleanup: temp file deleted on success and on non-resumable failures;
//          kept on incomplete download so the next attempt can resume.
// ─────────────────────────────────────────────────────────────────────────────

#if FOTA_USE_SD_TEMP
/** @brief Download to SD, verify, then flash in two passes for low-RAM operation. */
FotaResult FotaClient::_sdVerifyFlash(const FotaManifest &manifest)
{
	LOGI("SD update workflow started for version=%s", manifest.version);
	static const size_t CHUNK_SIZE = 4096;

	// Use the shared download client from performUpdate() to enable TLS
	// session resumption across retries. Fall back to a local client if
	// called outside performUpdate().
	WiFiClientSecure localClient;
	if (!_dlClient)
		_configureWifiClient(localClient);
	WiFiClientSecure &wifiClient = _dlClient ? *_dlClient : localClient;

	// ── Chunk buffer (shared by resume-rehash and download) ────────────────
	uint8_t *chunk = (uint8_t *)malloc(CHUNK_SIZE);
	if (!chunk)
	{
		_setError("SD: chunk malloc failed");
		return FotaResult::ERR_ALLOC;
	}

	// ── SHA-256 context ────────────────────────────────────────────────────
	mbedtls_sha256_context shaCtx;
	mbedtls_sha256_init(&shaCtx);
	mbedtls_sha256_starts(&shaCtx, 0);

	// ── Check for a partial download from a previous attempt ───────────────
	size_t existingBytes = 0;
#if FOTA_RESUME_SD_DOWNLOAD
	{
		File chk = FOTA_SD_FS.open(_sdTempPath, FILE_READ);
		if (chk)
		{
			const size_t sz = chk.size();
			const size_t maxExpected = manifest.file_size
										   ? (size_t)manifest.file_size
										   : FOTA_MAX_FIRMWARE_SIZE;
			if (sz > 0 && sz < maxExpected)
			{
				// Re-feed existing bytes into the SHA context before continuing.
				size_t rehashed = 0;
				bool rehashOk = true;
				while (chk.available())
				{
					int got = chk.read(chunk, CHUNK_SIZE);
					if (got <= 0)
					{
						rehashOk = false;
						break;
					}
					mbedtls_sha256_update(&shaCtx, chunk, got);
					rehashed += got;
				}
				chk.close();

				if (rehashOk && rehashed == sz)
				{
					existingBytes = sz;
					LOGI("SD: Resume from byte %u (rehash OK)", (unsigned)existingBytes);
				}
				else
				{
					// Corrupt partial — reset and start fresh.
					LOGW("SD: Partial file rehash mismatch — restarting download");
					mbedtls_sha256_free(&shaCtx);
					mbedtls_sha256_init(&shaCtx);
					mbedtls_sha256_starts(&shaCtx, 0);
					FOTA_SD_FS.remove(_sdTempPath);
				}
			}
			else
			{
				chk.close();
				if (sz > 0) // stale complete file
					FOTA_SD_FS.remove(_sdTempPath);
			}
		}
	}
#else
	if (FOTA_SD_FS.exists(_sdTempPath))
		FOTA_SD_FS.remove(_sdTempPath);
#endif

	// ── HTTP request ───────────────────────────────────────────────────────
	HTTPClient http;
	if (!http.begin(wifiClient, manifest.download_url))
	{
		_setError("SD: Download begin() failed");
		free(chunk);
		mbedtls_sha256_free(&shaCtx);
		return FotaResult::ERR_DOWNLOAD;
	}
	http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
	if (_authToken[0] != '\0')
	{
		char bearer[520];
		snprintf(bearer, sizeof(bearer), "Bearer %s", _authToken);
		http.addHeader("Authorization", bearer);
	}
	http.addHeader("Accept", "application/octet-stream");
	http.addHeader("User-Agent", FOTA_USER_AGENT);
	http.setTimeout((uint16_t)(FOTA_DOWNLOAD_TIMEOUT_MS > 65535u ? 65535u : FOTA_DOWNLOAD_TIMEOUT_MS));

#if FOTA_RESUME_SD_DOWNLOAD
	if (existingBytes > 0)
	{
		char rangeHdr[48];
		snprintf(rangeHdr, sizeof(rangeHdr), "bytes=%u-", (unsigned)existingBytes);
		http.addHeader("Range", rangeHdr);
	}
#endif

	int httpCode = http.GET();
	LOGD("SD download HTTP response=%d", httpCode);
	bool resuming = (httpCode == HTTP_CODE_PARTIAL_CONTENT); // 206

	if (httpCode == HTTP_CODE_OK && existingBytes > 0)
	{
		// Server ignored Range — reset SHA and restart from byte 0.
		LOGW("SD: Server returned 200 (no Range support) — restarting download");
		mbedtls_sha256_free(&shaCtx);
		mbedtls_sha256_init(&shaCtx);
		mbedtls_sha256_starts(&shaCtx, 0);
		existingBytes = 0;
		if (FOTA_SD_FS.exists(_sdTempPath))
			FOTA_SD_FS.remove(_sdTempPath);
	}

	if (httpCode != HTTP_CODE_OK && !resuming)
	{
		String errBody = http.getString();
		if (httpCode == HTTP_CODE_FORBIDDEN || httpCode == 410)
			_setError("SD: Download URL expired or forbidden (HTTP %d) — retrying with fresh manifest", httpCode);
		else
			_setError("SD: Download HTTP %d: %.120s", httpCode, errBody.c_str());
		free(chunk);
		mbedtls_sha256_free(&shaCtx);
		http.end();
		return (httpCode == HTTP_CODE_FORBIDDEN || httpCode == 410)
				   ? FotaResult::ERR_EXPIRED
				   : FotaResult::ERR_DOWNLOAD;
	}

	int remainingLen = http.getSize();
	if (remainingLen <= 0)
	{
		_setError("SD: No Content-Length in response");
		free(chunk);
		mbedtls_sha256_free(&shaCtx);
		http.end();
		return FotaResult::ERR_DOWNLOAD;
	}
	// For 206: Content-Length = remaining bytes; total = existing + remaining.
	// For 200: Content-Length = full firmware size.
	const size_t totalSize = existingBytes + (size_t)remainingLen;
	if (totalSize > FOTA_MAX_FIRMWARE_SIZE)
	{
		_setError("SD: Firmware too large: %u > %d bytes", (unsigned)totalSize, FOTA_MAX_FIRMWARE_SIZE);
		free(chunk);
		mbedtls_sha256_free(&shaCtx);
		http.end();
		return FotaResult::ERR_SIZE;
	}

	// ── Pass 1: HTTP → SD temp file (+ rolling SHA-256) ───────────────────
	// Append if resuming; truncate-write otherwise.
	File tmpFile = FOTA_SD_FS.open(_sdTempPath, resuming ? FILE_APPEND : FILE_WRITE);
	if (!tmpFile)
	{
		_setError("SD: Cannot open temp file: %s", _sdTempPath);
		free(chunk);
		mbedtls_sha256_free(&shaCtx);
		http.end();
		return FotaResult::ERR_ALLOC;
	}

	WiFiClient *stream = http.getStreamPtr();
	size_t bytesWritten = existingBytes; // total = existing + new
	const unsigned long deadline = millis() + FOTA_DOWNLOAD_TIMEOUT_MS;

	while (bytesWritten < totalSize && millis() < deadline)
	{
		int avail = stream->available();
		if (avail > 0)
		{
			size_t toRead = (size_t)avail < CHUNK_SIZE ? (size_t)avail : CHUNK_SIZE;
			size_t remaining = totalSize - bytesWritten;
			if (toRead > remaining)
				toRead = remaining;
			int got = stream->read(chunk, toRead);
			if (got > 0)
			{
				mbedtls_sha256_update(&shaCtx, chunk, got);
				if (tmpFile.write(chunk, got) != (size_t)got)
				{
					_setError("SD: Write failed at offset %u", (unsigned)bytesWritten);
					free(chunk);
					mbedtls_sha256_free(&shaCtx);
					tmpFile.close();
					FOTA_SD_FS.remove(_sdTempPath);
					http.end();
					return FotaResult::ERR_DOWNLOAD;
				}
				bytesWritten += got;
				if ((bytesWritten % (64 * 1024)) < (size_t)got || bytesWritten == totalSize)
					LOGV("SD download progress: %u/%u bytes", (unsigned)bytesWritten, (unsigned)totalSize);
			}
		}
		else
		{
			delay(1);
		}
#if FOTA_WATCHDOG_FEED
		yield();
#endif
	}

	tmpFile.close();
	http.end();

	if (bytesWritten != totalSize)
	{
		_setError("SD: Download incomplete: %u/%u bytes", (unsigned)bytesWritten, (unsigned)totalSize);
		free(chunk);
		mbedtls_sha256_free(&shaCtx);
		// Keep partial file for next resume attempt — do NOT delete it.
		LOGI("SD: Partial file preserved at %s for next retry", _sdTempPath);
		return FotaResult::ERR_DOWNLOAD;
	}
	LOGI("SD: Downloaded %u bytes → %s", (unsigned)bytesWritten, _sdTempPath);

	// ── Verify SHA-256 BEFORE touching the OTA partition ──────────────────
	_emit("VERIFYING", manifest.version);
	uint8_t hash32[32];
	mbedtls_sha256_finish(&shaCtx, hash32);
	mbedtls_sha256_free(&shaCtx);

#if FOTA_VERIFY_SHA256
	if (fotaVerifySha256Digest(hash32, manifest.hash) != 0)
	{
		_setError("SD: SHA-256 mismatch for firmware v%s", manifest.version);
		free(chunk);
		FOTA_SD_FS.remove(_sdTempPath);
		return FotaResult::ERR_SHA256;
	}
	LOGI("SD: SHA-256 OK");
#endif

#if FOTA_VERIFY_SIGNATURE
	if (_publicKeyPem[0] == '\0')
	{
		_setError("SD: No public key for signature verification");
		free(chunk);
		FOTA_SD_FS.remove(_sdTempPath);
		return FotaResult::ERR_PUBKEY;
	}
	if (fotaVerifySignature(hash32, manifest.signature,
							_publicKeyPem, manifest.signature_algorithm) != 0)
	{
		_setError("SD: Signature verification failed for firmware v%s", manifest.version);
		free(chunk);
		FOTA_SD_FS.remove(_sdTempPath);
		return FotaResult::ERR_SIGNATURE;
	}
	LOGI("SD: Signature OK");
#endif

	// ── Pass 2: Flash OTA partition from SD temp file ──────────────────────
	_emit("INSTALLING", manifest.version);
	const esp_partition_t *updatePart = esp_ota_get_next_update_partition(nullptr);
	if (!updatePart)
	{
		_setError("SD: No OTA partition found");
		free(chunk);
		FOTA_SD_FS.remove(_sdTempPath);
		return FotaResult::ERR_FLASH;
	}

	esp_ota_handle_t otaHandle = 0;
	if (esp_ota_begin(updatePart, totalSize, &otaHandle) != ESP_OK)
	{
		_setError("SD: esp_ota_begin failed");
		free(chunk);
		FOTA_SD_FS.remove(_sdTempPath);
		return FotaResult::ERR_FLASH;
	}

	File flashFile = FOTA_SD_FS.open(_sdTempPath, FILE_READ);
	if (!flashFile)
	{
		_setError("SD: Cannot reopen temp file for flash: %s", _sdTempPath);
		free(chunk);
		esp_ota_abort(otaHandle);
		FOTA_SD_FS.remove(_sdTempPath);
		return FotaResult::ERR_FLASH;
	}

	size_t flashedBytes = 0;
	while (flashFile.available())
	{
		int got = flashFile.read(chunk, CHUNK_SIZE);
		if (got <= 0)
			break;
		if (esp_ota_write(otaHandle, chunk, got) != ESP_OK)
		{
			_setError("SD: esp_ota_write failed at offset %u", (unsigned)flashedBytes);
			free(chunk);
			flashFile.close();
			esp_ota_abort(otaHandle);
			FOTA_SD_FS.remove(_sdTempPath);
			return FotaResult::ERR_FLASH;
		}
		flashedBytes += got;
#if FOTA_WATCHDOG_FEED
		yield();
#endif
	}

	free(chunk);
	flashFile.close();

	// ── Auto-cleanup ───────────────────────────────────────────────────────
	FOTA_SD_FS.remove(_sdTempPath);
	LOGI("SD: Temp file deleted: %s", _sdTempPath);

	// ── Commit OTA partition ───────────────────────────────────────────────
	if (esp_ota_end(otaHandle) != ESP_OK)
	{
		_setError("SD: esp_ota_end failed");
		return FotaResult::ERR_FLASH;
	}
	if (esp_ota_set_boot_partition(updatePart) != ESP_OK)
	{
		_setError("SD: esp_ota_set_boot_partition failed");
		return FotaResult::ERR_FLASH;
	}

	LOGI("SD: OTA flash committed for v%s", manifest.version);
	return FotaResult::OK;
}
#endif // FOTA_USE_SD_TEMP

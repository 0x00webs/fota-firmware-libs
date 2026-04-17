/**
 * @file FotaClient.cpp
 * @brief Secure ESP8266 OTA client — check, verify, and flash pipeline.
 *
 * Uses BearSSL (via ESP8266WiFi) for TLS and the Arduino Updater (Update.h)
 * for OTA flash. ArduinoJson 7 is required for JSON parsing.
 */

#include "FotaClient.h"
#include "FotaVerify.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <ArduinoJson.h>
#include <Updater.h>
#include <bearssl/bearssl_hash.h>

// ─── Logging macros (Serial-based, no ESP-IDF on ESP8266) ────────────────────

#if FOTA_LOG_LEVEL >= 4
#define LOGV(fmt, ...) Serial.printf("[V][FotaClient] " fmt "\r\n", ##__VA_ARGS__)
#else
#define LOGV(fmt, ...) ((void)0)
#endif

#if FOTA_LOG_LEVEL >= 3
#define LOGI(fmt, ...) Serial.printf("[I][FotaClient] " fmt "\r\n", ##__VA_ARGS__)
#else
#define LOGI(fmt, ...) ((void)0)
#endif

#if FOTA_LOG_LEVEL >= 2
#define LOGW(fmt, ...) Serial.printf("[W][FotaClient] " fmt "\r\n", ##__VA_ARGS__)
#else
#define LOGW(fmt, ...) ((void)0)
#endif

#if FOTA_LOG_LEVEL >= 1
#define LOGE(fmt, ...) Serial.printf("[E][FotaClient] " fmt "\r\n", ##__VA_ARGS__)
#else
#define LOGE(fmt, ...) ((void)0)
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

FotaClient::FotaClient()
	: _checkIntervalSecs(0),
	  _bearCACert(nullptr),
	  _bearClientCert(nullptr),
	  _bearClientKey(nullptr),
	  _eventCb(nullptr),
	  _retryCount(FOTA_RETRY_COUNT),
	  _retryDelayMs(FOTA_RETRY_DELAY_MS),
	  _state(FotaState::IDLE),
	  _stats()
{
	_serverUrl[0] = '\0';
	_hardwareModel[0] = '\0';
	_currentVersion[0] = '\0';
	_authToken[0] = '\0';
	_publicKeyPem[0] = '\0';
	_lastError[0] = '\0';
	_deviceId[0] = '\0';
	memset(&_stats, 0, sizeof(_stats));
	_stats.lastResult = FotaResult::NO_UPDATE;
}

FotaClient::~FotaClient()
{
	delete _bearCACert;
	delete _bearClientCert;
	delete _bearClientKey;
}

// ─────────────────────────────────────────────────────────────────────────────
// begin
// ─────────────────────────────────────────────────────────────────────────────

void FotaClient::begin(const char *serverUrl,
					   const char *hardwareModel,
					   const char *currentVersion,
					   const char *authToken,
					   const char *deviceId)
{
	strncpy(_serverUrl, serverUrl, sizeof(_serverUrl) - 1);
	strncpy(_hardwareModel, hardwareModel, sizeof(_hardwareModel) - 1);
	strncpy(_currentVersion, currentVersion, sizeof(_currentVersion) - 1);

	if (authToken && authToken[0] != '\0')
		strncpy(_authToken, authToken, sizeof(_authToken) - 1);

	if (deviceId && deviceId[0] != '\0')
		strncpy(_deviceId, deviceId, sizeof(_deviceId) - 1);

	if (FOTA_SIGNING_PUBLIC_KEY[0] != '\0')
		strncpy(_publicKeyPem, FOTA_SIGNING_PUBLIC_KEY, sizeof(_publicKeyPem) - 1);

	LOGI("FotaClient v1.0.0 (ESP8266) | server=%s hw=%s ver=%s",
		 _serverUrl, _hardwareModel, _currentVersion);
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration setters
// ─────────────────────────────────────────────────────────────────────────────

void FotaClient::setAuthToken(const char *token)
{
	strncpy(_authToken, token, sizeof(_authToken) - 1);
}

void FotaClient::setDeviceId(const char *deviceId)
{
	strncpy(_deviceId, deviceId, sizeof(_deviceId) - 1);
}

void FotaClient::setCACert(const char *pem)
{
	delete _bearCACert;
	_bearCACert = nullptr;
	if (pem)
	{
		_bearCACert = new BearSSL::X509List(pem);
	}
}

void FotaClient::setClientCert(const char *pem)
{
	delete _bearClientCert;
	_bearClientCert = nullptr;
	if (pem)
		_bearClientCert = new BearSSL::X509List(pem);
}

void FotaClient::setClientKey(const char *pem)
{
	delete _bearClientKey;
	_bearClientKey = nullptr;
	if (pem)
		_bearClientKey = new BearSSL::PrivateKey(pem);
}

void FotaClient::setPublicKey(const char *pem)
{
	strncpy(_publicKeyPem, pem, sizeof(_publicKeyPem) - 1);
}

void FotaClient::onEvent(FotaEventCallback cb)
{
	_eventCb = cb;
}

void FotaClient::setRetryCount(uint8_t count)
{
	_retryCount = count;
}

void FotaClient::setRetryDelay(uint32_t ms)
{
	_retryDelayMs = ms;
}

void FotaClient::resetStats()
{
	memset(&_stats, 0, sizeof(_stats));
	_stats.lastResult = FotaResult::NO_UPDATE;
}

// ─────────────────────────────────────────────────────────────────────────────
// _configureClient — apply BearSSL trust anchor + optional mTLS
// ─────────────────────────────────────────────────────────────────────────────

void FotaClient::_configureClient(BearSSL::WiFiClientSecure &c) const
{
	if (_bearCACert)
	{
		c.setTrustAnchors(_bearCACert);
	}
	else
	{
		c.setInsecure();
		LOGW("TLS peer verification disabled — use setCACert() in production");
	}
	if (_bearClientCert && _bearClientKey)
		c.setClientRSACert(_bearClientCert, _bearClientKey);
}

// ─────────────────────────────────────────────────────────────────────────────
// _emit — fire user callback and report progress to platform
// ─────────────────────────────────────────────────────────────────────────────

void FotaClient::_emit(const char *stage, const char *version, const char *error)
{
	const char *err = error ? error : "";
	FotaState prevState = _state;

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
		LOGV("State %d -> %d at stage=%s", (int)prevState, (int)_state, stage);

	if (_eventCb)
		_eventCb(stage, version, err);

	if (strcmp(stage, "CHECKING") != 0 && strcmp(stage, "NO_UPDATE") != 0)
		reportProgress(version, stage, (err[0] != '\0') ? err : nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// _setError
// ─────────────────────────────────────────────────────────────────────────────

void FotaClient::_setError(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vsnprintf(_lastError, sizeof(_lastError), fmt, args);
	va_end(args);
	LOGE("%s", _lastError);
}

// ─────────────────────────────────────────────────────────────────────────────
// _addAuthHeader
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// _httpGet
// ─────────────────────────────────────────────────────────────────────────────

int FotaClient::_httpGet(const char *url, char *respBuf, size_t respBufLen)
{
	LOGV("HTTP GET: %s", url);
	BearSSL::WiFiClientSecure client;
	_configureClient(client);
	client.setTimeout(FOTA_CONNECT_TIMEOUT_MS / 1000);

	HTTPClient http;
	if (!http.begin(client, url))
	{
		_setError("HTTPClient.begin() failed: %s", url);
		return -1;
	}

	if (!_addAuthHeader(http))
	{
		http.end();
		return -2;
	}
	http.addHeader("Accept", "application/json");
	http.addHeader("User-Agent", FOTA_USER_AGENT);
	http.setTimeout(FOTA_HTTP_TIMEOUT_MS);

	// Feed both watchdogs before and after the blocking HTTP call.
	// On ESP8266, yield() drives background networking and keeps SWDT alive.
	ESP.wdtFeed();
	yield();
	int httpCode = http.GET();
	ESP.wdtFeed();
	yield();
	LOGV("HTTP GET response code=%d", httpCode);

	if (httpCode <= 0)
	{
		_setError("HTTP GET error: %s", http.errorToString(httpCode).c_str());
		http.end();
		return httpCode;
	}

	if (httpCode == HTTP_CODE_OK)
	{
		String body = http.getString();
		strncpy(respBuf, body.c_str(), respBufLen - 1);
		respBuf[respBufLen - 1] = '\0';
	}

	http.end();
	return httpCode;
}

// ─────────────────────────────────────────────────────────────────────────────
// _httpPost
// ─────────────────────────────────────────────────────────────────────────────

int FotaClient::_httpPost(const char *url, const char *jsonBody,
						  char *respBuf, size_t respBufLen)
{
	LOGV("HTTP POST: %s", url);
	BearSSL::WiFiClientSecure client;
	_configureClient(client);
	client.setTimeout(FOTA_CONNECT_TIMEOUT_MS / 1000);

	HTTPClient http;
	if (!http.begin(client, url))
	{
		_setError("HTTPClient.begin() failed for POST: %s", url);
		return -1;
	}

	if (!_addAuthHeader(http))
	{
		http.end();
		return -2;
	}
	http.addHeader("Content-Type", "application/json");
	http.addHeader("User-Agent", FOTA_USER_AGENT);
	http.setTimeout(FOTA_HTTP_TIMEOUT_MS);

	// See _httpGet() watchdog rationale.
	ESP.wdtFeed();
	yield();
	int httpCode = http.POST((uint8_t *)jsonBody, strlen(jsonBody));
	ESP.wdtFeed();
	yield();
	LOGV("HTTP POST response code=%d", httpCode);

	if (httpCode <= 0)
	{
		_setError("HTTP POST error: %s", http.errorToString(httpCode).c_str());
		http.end();
		return httpCode;
	}

	if (respBuf && respBufLen > 0 && httpCode == HTTP_CODE_OK)
	{
		String body = http.getString();
		strncpy(respBuf, body.c_str(), respBufLen - 1);
		respBuf[respBufLen - 1] = '\0';
	}

	http.end();
	return httpCode;
}

// ─────────────────────────────────────────────────────────────────────────────
// fetchPublicKey
// ─────────────────────────────────────────────────────────────────────────────

FotaResult FotaClient::fetchPublicKey(const char *expectedKeyId)
{
	char url[512];
	snprintf(url, sizeof(url), "%s%s/firmware/public-key", _serverUrl, FOTA_API_PREFIX);

	char respBuf[4096];
	int httpCode = _httpGet(url, respBuf, sizeof(respBuf));
	if (httpCode != HTTP_CODE_OK)
	{
		if (httpCode == -2)
			return FotaResult::ERR_TOKEN;
		_setError("fetchPublicKey: HTTP %d", httpCode);
		return FotaResult::ERR_HTTP;
	}

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

FotaResult FotaClient::checkForUpdate(FotaManifest &manifest)
{
	if (_authToken[0] == '\0')
		return FotaResult::ERR_TOKEN;

	char url[512];
	if (_deviceId[0] != '\0')
	{
		snprintf(url, sizeof(url),
				 "%s%s/ota/check?hardware=%s&version=%s&device_id=%s",
				 _serverUrl, FOTA_API_PREFIX,
				 _hardwareModel, _currentVersion, _deviceId);
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
	manifest.check_interval_secs = doc["check_interval_seconds"] | 0u;
	if (manifest.check_interval_secs > 0)
		_checkIntervalSecs = manifest.check_interval_secs;

	if (!manifest.update_available)
	{
		LOGI("No update available");
		return FotaResult::NO_UPDATE;
	}

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
// reportProgress
// ─────────────────────────────────────────────────────────────────────────────

FotaResult FotaClient::reportProgress(const char *targetVersion,
									  const char *status,
									  const char *errorMsg)
{
#if FOTA_REPORT_PROGRESS
	if (_deviceId[0] == '\0')
		return FotaResult::OK;

	char url[512];
	snprintf(url, sizeof(url), "%s%s/ota/device/progress", _serverUrl, FOTA_API_PREFIX);

	char body[512];
	if (errorMsg && errorMsg[0] != '\0')
	{
		// Escape double-quotes to keep JSON valid
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
		LOGW("reportProgress HTTP %d for status=%s (non-fatal)", httpCode, status);
	else
		LOGI("Progress reported: v%s -> %s", targetVersion, status);
#else
	(void)targetVersion;
	(void)status;
	(void)errorMsg;
#endif
	return FotaResult::OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// _streamVerifyFlash
//
// Single-pass: stream HTTP response → Update.write() + SHA-256 feed
// After all bytes received: verify SHA-256 + signature → Update.end()
// ─────────────────────────────────────────────────────────────────────────────

FotaResult FotaClient::_streamVerifyFlash(const FotaManifest &manifest)
{
	LOGI("Download started: v%s (%u bytes)", manifest.version, manifest.file_size);

	BearSSL::WiFiClientSecure client;
	_configureClient(client);
	client.setTimeout(FOTA_DOWNLOAD_TIMEOUT_MS / 1000);

	HTTPClient http;
	if (!http.begin(client, manifest.download_url))
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
	http.setTimeout((uint16_t)(FOTA_DOWNLOAD_TIMEOUT_MS > 65535u ? 65535u : FOTA_DOWNLOAD_TIMEOUT_MS)); // Clamp to uint16_t max

	// See _httpGet() watchdog rationale.
	ESP.wdtFeed();
	yield();
	int httpCode = http.GET();
	ESP.wdtFeed();
	yield();
	if (httpCode != HTTP_CODE_OK)
	{
		if (httpCode == HTTP_CODE_FORBIDDEN || httpCode == 410)
			_setError("Download URL expired (HTTP %d)", httpCode);
		else
			_setError("Download HTTP %d", httpCode);
		http.end();
		return (httpCode == HTTP_CODE_FORBIDDEN || httpCode == 410)
				   ? FotaResult::ERR_EXPIRED
				   : FotaResult::ERR_DOWNLOAD;
	}

	int contentLen = http.getSize();
	if (contentLen <= 0)
	{
		_setError("Download: no Content-Length");
		http.end();
		return FotaResult::ERR_DOWNLOAD;
	}

	if ((size_t)contentLen > FOTA_MAX_FIRMWARE_SIZE)
	{
		_setError("Firmware too large: %d > %d bytes", contentLen, FOTA_MAX_FIRMWARE_SIZE);
		http.end();
		return FotaResult::ERR_SIZE;
	}

	// Begin OTA using the ESP8266 Arduino Updater
	if (!Update.begin((size_t)contentLen))
	{
		_setError("Update.begin() failed: %s", Update.getErrorString().c_str());
		http.end();
		return FotaResult::ERR_FLASH;
	}

	// Chunk buffer — keep it small to avoid heap pressure on the ESP8266
	static const size_t CHUNK_SIZE = 512;
	uint8_t *chunk = (uint8_t *)malloc(CHUNK_SIZE);
	if (!chunk)
	{
		_setError("chunk malloc failed");
		Update.end(false); // cancel — no abort() on ESP8266 UpdaterClass
		http.end();
		return FotaResult::ERR_ALLOC;
	}

	br_sha256_context shaCtx;
	br_sha256_init(&shaCtx);

	WiFiClient *stream = http.getStreamPtr();
	stream->setTimeout(1);
	size_t bytesWritten = 0;
	unsigned long deadline = millis() + FOTA_DOWNLOAD_TIMEOUT_MS;
	unsigned long lastProgressMs = millis();

	while (bytesWritten < (size_t)contentLen && millis() < deadline)
	{
		int avail = stream->available();
		if (avail > 0)
		{
			ESP.wdtFeed();
			yield();
			size_t toRead = (size_t)avail < CHUNK_SIZE ? (size_t)avail : CHUNK_SIZE;
			size_t remaining = (size_t)contentLen - bytesWritten;
			if (toRead > remaining)
				toRead = remaining;

			int got = stream->read(chunk, toRead);
			if (got > 0)
			{
				lastProgressMs = millis();
				br_sha256_update(&shaCtx, chunk, got);

				if (Update.write(chunk, got) != (size_t)got)
				{
					_setError("Update.write() failed at offset %u: %s",
							  (unsigned)bytesWritten,
							  Update.getErrorString().c_str());
					free(chunk);
					Update.end(false); // cancel
					http.end();
					return FotaResult::ERR_FLASH;
				}
				bytesWritten += got;
				ESP.wdtFeed();

				if ((bytesWritten % (32 * 1024)) < (size_t)got || bytesWritten == (size_t)contentLen)
					LOGV("Download progress: %u/%u bytes", (unsigned)bytesWritten, (unsigned)contentLen);
			}
		}
		else
		{
			delay(1);
			if ((millis() - lastProgressMs) > 8000)
			{
				_setError("Download stalled (no data for >8s)");
				free(chunk);
				Update.end(false); // cancel
				http.end();
				return FotaResult::ERR_DOWNLOAD;
			}
		}
#if FOTA_WATCHDOG_FEED
		yield();
#endif
	}

	free(chunk);
	http.end();

	if (bytesWritten != (size_t)contentLen)
	{
		_setError("Download incomplete: %u/%u bytes",
				  (unsigned)bytesWritten, (unsigned)contentLen);
		Update.end(false); // cancel
		return FotaResult::ERR_DOWNLOAD;
	}

	LOGI("Downloaded %u bytes", (unsigned)bytesWritten);

	// ── SHA-256 verification ───────────────────────────────────────────────
	uint8_t hash32[32];
	br_sha256_out(&shaCtx, hash32);

#if FOTA_VERIFY_SHA256
	if (fotaVerifySha256Digest(hash32, manifest.hash) != 0)
	{
		_setError("SHA-256 mismatch for firmware v%s", manifest.version);
		Update.end(false); // cancel
		return FotaResult::ERR_SHA256;
	}
	LOGI("SHA-256 OK");
#endif

	// ── Signature verification ─────────────────────────────────────────────
#if FOTA_VERIFY_SIGNATURE
	if (_publicKeyPem[0] == '\0')
	{
		_setError("No public key for signature verification");
		Update.end(false); // cancel
		return FotaResult::ERR_PUBKEY;
	}
	if (fotaVerifySignature(hash32, manifest.signature,
							_publicKeyPem, manifest.signature_algorithm) != 0)
	{
		_setError("Signature verification failed for firmware v%s", manifest.version);
		Update.end(false); // cancel
		return FotaResult::ERR_SIGNATURE;
	}
	LOGI("Signature OK");
#endif

	// ── Commit OTA ─────────────────────────────────────────────────────────
	if (!Update.end(true /* set boot partition */))
	{
		_setError("Update.end() failed: %s", Update.getErrorString().c_str());
		return FotaResult::ERR_FLASH;
	}

	LOGI("OTA flash committed for v%s", manifest.version);
	return FotaResult::OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// _doUpdate — single attempt
// ─────────────────────────────────────────────────────────────────────────────

FotaResult FotaClient::_doUpdate()
{
	LOGI("Update attempt started: current_version=%s", _currentVersion);
	_stats.checkCount++;
	_stats.lastCheckMs = (uint32_t)millis();
	_emit("CHECKING", _currentVersion);

	FotaManifest manifest = {};
	FotaResult r = checkForUpdate(manifest);
	if (r == FotaResult::NO_UPDATE)
	{
		_emit("NO_UPDATE", _currentVersion);
		return FotaResult::NO_UPDATE;
	}
	if (r != FotaResult::OK)
		return r;

	const uint32_t manifestFetchedAtMs = millis();

	// Auto-fetch public key if not already configured
#if FOTA_VERIFY_SIGNATURE && FOTA_AUTO_FETCH_PUBLIC_KEY
	if (_publicKeyPem[0] == '\0' && manifest.public_key_id[0] != '\0')
	{
		LOGI("Auto-fetching public key");
		FotaResult pkr = fetchPublicKey(nullptr);
		if (pkr != FotaResult::OK)
		{
			_emit("FAILED", manifest.version, _lastError);
			return FotaResult::ERR_PUBKEY;
		}
	}
#endif

#if FOTA_VERIFY_SIGNATURE
	if (_publicKeyPem[0] == '\0')
	{
		_setError("No public key. Call setPublicKey() or enable FOTA_AUTO_FETCH_PUBLIC_KEY.");
		_emit("FAILED", manifest.version, _lastError);
		return FotaResult::ERR_PUBKEY;
	}
#endif

	// Check URL expiry
	{
		const uint32_t elapsedSec = (uint32_t)((millis() - manifestFetchedAtMs) / 1000UL);
		if (manifest.expires_in > 0 &&
			elapsedSec + FOTA_URL_EXPIRY_MARGIN_S >= manifest.expires_in)
		{
			_setError("Download URL expired (age %us >= expires_in %us)",
					  (unsigned)elapsedSec, (unsigned)manifest.expires_in);
			_emit("FAILED", manifest.version, _lastError);
			return FotaResult::ERR_EXPIRED;
		}
	}

	_emit("DOWNLOADING", manifest.version);
	LOGI("Streaming flash v%s (%u bytes)...", manifest.version, manifest.file_size);

	r = _streamVerifyFlash(manifest);
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
// performUpdate — retry wrapper
// ─────────────────────────────────────────────────────────────────────────────

FotaResult FotaClient::performUpdate()
{
	LOGI("performUpdate() started");
	const uint8_t maxAttempts = (_retryCount < 10 ? _retryCount : 10) + 1;
	FotaResult r = FotaResult::ERR_HTTP;

	for (uint8_t attempt = 0; attempt < maxAttempts; attempt++)
	{
		LOGV("performUpdate attempt %u/%u", (unsigned)(attempt + 1), (unsigned)maxAttempts);
		if (attempt > 0)
		{
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
		ESP.restart();
	}
#else
	if (r == FotaResult::OK)
		LOGI("Flash done. Call ESP.restart() to boot the new firmware.");
#endif

	return r;
}

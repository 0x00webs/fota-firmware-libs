/*
 * FsotaClient.cpp — NodeWave Filesystem OTA client for ESP8266
 *
 * Applies LittleFS / SPIFFS / FATFS images served by the FOTA platform.
 *
 * Flash is performed via the Arduino Updater library with the U_FS flag, which
 * targets the filesystem partition automatically — no partition label required.
 *
 * SHA-256 is computed with BearSSL's br_sha256 (always present on ESP8266).
 */

#include "FsotaClient.h"
#include <ArduinoJson.h>
#include <Updater.h>
#include <bearssl/bearssl_hash.h>

// Internal chunk size for download + write loop (bytes)
#define FSOTA_STREAM_CHUNK_SIZE 512

// ── Constructor / Destructor ──────────────────────────────────────────────────

FsotaClient::FsotaClient()
	: _caCert(nullptr),
	  _x509(nullptr),
	  _wc(nullptr),
	  _lastResult(FsotaResult::NO_UPDATE),
	  _cb(nullptr)
{
	strncpy(_serverUrl, FSOTA_SERVER_URL, sizeof(_serverUrl) - 1);
	strncpy(_hardwareModel, FOTA_HARDWARE_MODEL, sizeof(_hardwareModel) - 1);
	strncpy(_currentFsVersion, FSOTA_CURRENT_FS_VERSION, sizeof(_currentFsVersion) - 1);
	strncpy(_fsType, FSOTA_FS_TYPE, sizeof(_fsType) - 1);
	strncpy(_authToken, FOTA_AUTH_TOKEN, sizeof(_authToken) - 1);
	_lastError[0] = '\0';
}

FsotaClient::~FsotaClient()
{
	if (_x509)
	{
		delete _x509;
		_x509 = nullptr;
	}
	if (_wc)
	{
		delete _wc;
		_wc = nullptr;
	}
}

// ── Configuration setters ─────────────────────────────────────────────────────

void FsotaClient::setServerUrl(const char *url)
{
	strncpy(_serverUrl, url, sizeof(_serverUrl) - 1);
}

void FsotaClient::setHardwareModel(const char *model)
{
	strncpy(_hardwareModel, model, sizeof(_hardwareModel) - 1);
}

void FsotaClient::setCurrentFsVersion(const char *version)
{
	strncpy(_currentFsVersion, version, sizeof(_currentFsVersion) - 1);
}

void FsotaClient::setFsType(const char *fsType)
{
	strncpy(_fsType, fsType, sizeof(_fsType) - 1);
}

void FsotaClient::setAuthToken(const char *token)
{
	strncpy(_authToken, token, sizeof(_authToken) - 1);
}

void FsotaClient::setServerCACert(const char *pem)
{
	_caCert = pem;
	// Rebuild X509List if a previous one exists
	if (_x509)
	{
		delete _x509;
		_x509 = nullptr;
	}
	if (pem)
	{
		_x509 = new BearSSL::X509List(pem);
	}
}

// ── Private helpers ───────────────────────────────────────────────────────────

BearSSL::WiFiClientSecure *FsotaClient::_createWifiClient()
{
	auto *wc = new BearSSL::WiFiClientSecure();
	if (!wc)
		return nullptr;

	if (_x509)
		wc->setTrustAnchors(_x509);
	else
		wc->setInsecure(); // ⚠️ TLS peer verification disabled — development only

	return wc;
}

void FsotaClient::_releaseWifiClient(BearSSL::WiFiClientSecure *client)
{
	if (client)
		delete client;
}

void FsotaClient::_addAuthHeader(ESP8266HTTPClient &http)
{
	if (_authToken[0] != '\0')
	{
		String hdr = "Bearer ";
		hdr += _authToken;
		http.addHeader("Authorization", hdr);
	}
	http.addHeader("User-Agent", FSOTA_USER_AGENT);
}

void FsotaClient::_emit(const char *stage, const char *version, const char *error)
{
	if (_cb)
		_cb(stage, version ? version : "", error ? error : "");
}

FsotaResult FsotaClient::_setError(FsotaResult code, const char *msg)
{
	strncpy(_lastError, msg ? msg : "", sizeof(_lastError) - 1);
	_lastError[sizeof(_lastError) - 1] = '\0';
	return (_lastResult = code);
}

// ── Public API ────────────────────────────────────────────────────────────────

FsotaResult FsotaClient::checkForUpdate(FsotaManifest &manifest)
{
	if (WiFi.status() != WL_CONNECTED)
		return _setError(FsotaResult::ERR_WIFI, "WiFi not connected");

	if (_authToken[0] == '\0')
		return _setError(FsotaResult::ERR_TOKEN, "Auth token not set");

	BearSSL::WiFiClientSecure *wc = _createWifiClient();
	if (!wc)
		return _setError(FsotaResult::ERR_ALLOC, "Failed to allocate WiFiClientSecure");

	ESP8266HTTPClient http;
	String url = String(_serverUrl) + FOTA_API_PREFIX + "/filesystem/check" + "?hardware=" + _hardwareModel + "&fs_version=" + _currentFsVersion + "&fs_type=" + _fsType;

	http.setConnectTimeout(FOTA_CONNECT_TIMEOUT_MS);
	http.setTimeout(FOTA_HTTP_TIMEOUT_MS);

	if (!http.begin(*wc, url))
	{
		_releaseWifiClient(wc);
		return _setError(FsotaResult::ERR_HTTP, "http.begin() failed");
	}

	_addAuthHeader(http);

	int code = http.GET();
	if (code != HTTP_CODE_OK)
	{
		http.end();
		_releaseWifiClient(wc);
		char msg[48];
		snprintf(msg, sizeof(msg), "Check HTTP %d", code);
		return _setError(FsotaResult::ERR_HTTP, msg);
	}

	String body = http.getString();
	http.end();
	_releaseWifiClient(wc);

	JsonDocument doc;
	DeserializationError dErr = deserializeJson(doc, body);
	if (dErr)
		return _setError(FsotaResult::ERR_JSON, "JSON parse error");

	manifest.update_available = doc["update_available"] | false;
	if (!manifest.update_available)
		return FsotaResult::NO_UPDATE;

	strncpy(manifest.image_id, doc["image_id"] | "", sizeof(manifest.image_id) - 1);
	strncpy(manifest.version, doc["version"] | "", sizeof(manifest.version) - 1);
	strncpy(manifest.hardware_model, doc["hardware_model"] | "", sizeof(manifest.hardware_model) - 1);
	strncpy(manifest.fs_type, doc["fs_type"] | "", sizeof(manifest.fs_type) - 1);
	strncpy(manifest.hash, doc["checksum"] | "", sizeof(manifest.hash) - 1);
	strncpy(manifest.hash_algorithm, doc["algorithm"] | "sha256", sizeof(manifest.hash_algorithm) - 1);
	strncpy(manifest.download_url, doc["download_url"] | "", sizeof(manifest.download_url) - 1);
	strncpy(manifest.changelog, doc["changelog"] | "", sizeof(manifest.changelog) - 1);
	manifest.file_size = doc["file_size"] | 0;
	manifest.expires_in = doc["expires_in"] | 300;

	if (manifest.download_url[0] == '\0')
		return _setError(FsotaResult::ERR_HTTP, "No download_url in response");

	return FsotaResult::OK;
}

FsotaResult FsotaClient::performUpdate()
{
	_emit("CHECKING", _currentFsVersion);

	FsotaManifest manifest{};
	FsotaResult checkResult = checkForUpdate(manifest);

	if (checkResult == FsotaResult::NO_UPDATE)
	{
		_emit("NO_UPDATE", _currentFsVersion);
		return (_lastResult = FsotaResult::NO_UPDATE);
	}
	if (checkResult != FsotaResult::OK)
	{
		_emit("FAILED", _currentFsVersion, _lastError);
		return checkResult;
	}

	_emit("DOWNLOADING", manifest.version);

	FsotaResult wr = _streamAndFlash(
		manifest.download_url,
		FSOTA_VERIFY_SHA256 ? manifest.hash : "",
		manifest.file_size);

	if (wr != FsotaResult::OK)
	{
		_emit("FAILED", manifest.version, _lastError);
		return wr;
	}

	_emit("COMPLETED", manifest.version);

#if FSOTA_REBOOT_ON_SUCCESS
	ESP.restart();
#endif

	return (_lastResult = FsotaResult::OK);
}

FsotaResult FsotaClient::_streamAndFlash(
	const char *url,
	const char *expectedHash,
	uint32_t expectedSize)
{
	BearSSL::WiFiClientSecure *wc = _createWifiClient();
	if (!wc)
		return _setError(FsotaResult::ERR_ALLOC, "Failed to allocate WiFiClientSecure");

	ESP8266HTTPClient http;
	http.setConnectTimeout(FOTA_CONNECT_TIMEOUT_MS);
	http.setTimeout(FSOTA_DOWNLOAD_TIMEOUT_MS);
	http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // Follow 302 to Supabase signed URL

	if (!http.begin(*wc, url))
	{
		_releaseWifiClient(wc);
		return _setError(FsotaResult::ERR_HTTP, "http.begin() failed for download");
	}

	_addAuthHeader(http);

	int code = http.GET();
	if (code != HTTP_CODE_OK)
	{
		http.end();
		_releaseWifiClient(wc);
		char msg[48];
		snprintf(msg, sizeof(msg), "Download HTTP %d", code);
		return _setError(FsotaResult::ERR_HTTP, msg);
	}

	int contentLen = http.getSize();
	uint32_t totalBytes = (contentLen > 0) ? (uint32_t)contentLen : expectedSize;

	if (expectedSize > 0 && contentLen > 0 && (uint32_t)contentLen != expectedSize)
	{
		http.end();
		_releaseWifiClient(wc);
		return _setError(FsotaResult::ERR_SIZE, "Content-Length mismatch");
	}

	// Start Updater in filesystem mode
	if (!Update.begin(totalBytes > 0 ? totalBytes : UPDATE_SIZE_UNKNOWN, U_FS))
	{
		http.end();
		_releaseWifiClient(wc);
		char msg[64];
		snprintf(msg, sizeof(msg), "Update.begin() failed: %u", (unsigned)Update.getError());
		return _setError(FsotaResult::ERR_FLASH, msg);
	}

	// SHA-256 context (BearSSL)
	bool doHash = (expectedHash && expectedHash[0] != '\0');
	br_sha256_context shaCtx;
	if (doHash)
		br_sha256_init(&shaCtx);

	// Allocate chunk buffer
	uint8_t *chunk = (uint8_t *)malloc(FSOTA_STREAM_CHUNK_SIZE);
	if (!chunk)
	{
		Update.end(false);
		http.end();
		_releaseWifiClient(wc);
		return _setError(FsotaResult::ERR_ALLOC, "Chunk buffer allocation failed");
	}

	WiFiClient *stream = http.getStreamPtr();
	uint32_t offset = 0;
	uint32_t remaining = totalBytes;
	unsigned long dlStart = millis();
	FsotaResult result = FsotaResult::OK;

	_emit("INSTALLING", "");

	while (http.connected() && (remaining > 0 || totalBytes == 0))
	{
		if (millis() - dlStart > FSOTA_DOWNLOAD_TIMEOUT_MS)
		{
			result = _setError(FsotaResult::ERR_DOWNLOAD, "Download timeout");
			break;
		}

		int avail = stream->available();
		if (avail == 0)
		{
			delay(1);
			yield();
			continue;
		}

		int toRead = (avail < FSOTA_STREAM_CHUNK_SIZE) ? avail : FSOTA_STREAM_CHUNK_SIZE;
		if (remaining > 0)
			toRead = (toRead < (int)remaining) ? toRead : (int)remaining;

		int got = stream->readBytes(chunk, toRead);
		if (got <= 0)
			break;

		if (doHash)
			br_sha256_update(&shaCtx, chunk, got);

		size_t written = Update.write(chunk, got);
		if (written != (size_t)got)
		{
			char msg[64];
			snprintf(msg, sizeof(msg), "Update.write() failed: %u", (unsigned)Update.getError());
			result = _setError(FsotaResult::ERR_FLASH, msg);
			break;
		}

		offset += got;
		if (remaining > 0)
			remaining -= got;

		yield();
	}

	http.end();
	_releaseWifiClient(wc);
	free(chunk);

	if (result != FsotaResult::OK)
	{
		Update.end(false);
		if (doHash)
		{ /* br context is stack/register allocated — no free needed */
		}
		return result;
	}

	if (totalBytes > 0 && offset < totalBytes)
	{
		Update.end(false);
		return _setError(FsotaResult::ERR_DOWNLOAD, "Incomplete download");
	}

	// Verify SHA-256 before committing
	if (doHash)
	{
		_emit("VERIFYING", "");

		uint8_t digest[32];
		br_sha256_out(&shaCtx, digest);

		char hexDigest[65];
		for (int i = 0; i < 32; i++)
			snprintf(hexDigest + i * 2, 3, "%02x", digest[i]);
		hexDigest[64] = '\0';

		if (strcasecmp(hexDigest, expectedHash) != 0)
		{
			Update.end(false);
			char msg[96];
			snprintf(msg, sizeof(msg), "SHA-256 mismatch: got %s", hexDigest);
			return _setError(FsotaResult::ERR_SHA256, msg);
		}
	}

	if (!Update.end(true))
	{
		char msg[64];
		snprintf(msg, sizeof(msg), "Update.end() failed: %u", (unsigned)Update.getError());
		return _setError(FsotaResult::ERR_FLASH, msg);
	}

	return (_lastResult = FsotaResult::OK);
}

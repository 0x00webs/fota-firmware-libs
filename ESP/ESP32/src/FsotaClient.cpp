/*
 * FsotaClient.cpp — NodeWave Filesystem OTA client implementation for ESP32
 *
 * Applies LittleFS / SPIFFS / FATFS images served by the FOTA platform:
 *   1. GET /api/v1/filesystem/check  — version check + download URL
 *   2. HTTP GET download_url         — stream binary (302 redirect support)
 *   3. SHA-256 verify (optional)
 *   4. esp_partition_erase_range + esp_partition_write
 *   5. Return FsotaResult::OK — caller should call ESP.restart()
 */

#include "FsotaClient.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>

// Internal chunk buffer size for partition writes (must be a multiple of 4)
#define FSOTA_STREAM_CHUNK_SIZE 4096

// ── Constructor / Destructor ──────────────────────────────────────────────────

FsotaClient::FsotaClient()
	: _caCert(nullptr),
	  _lastResult(FsotaResult::NO_UPDATE),
	  _cb(nullptr),
	  _wifiClient(nullptr)
{
	strncpy(_serverUrl, FSOTA_SERVER_URL, sizeof(_serverUrl) - 1);
	strncpy(_hardwareModel, FOTA_HARDWARE_MODEL, sizeof(_hardwareModel) - 1);
	strncpy(_currentFsVersion, FSOTA_CURRENT_FS_VERSION, sizeof(_currentFsVersion) - 1);
	strncpy(_fsType, FSOTA_FS_TYPE, sizeof(_fsType) - 1);
	strncpy(_authToken, FOTA_AUTH_TOKEN, sizeof(_authToken) - 1);
	strncpy(_partitionLabel, FSOTA_PARTITION_LABEL, sizeof(_partitionLabel) - 1);
	_lastError[0] = '\0';
}

FsotaClient::~FsotaClient()
{
	if (_wifiClient)
	{
		delete _wifiClient;
		_wifiClient = nullptr;
	}
}

// ── Configuration setters ─────────────────────────────────────────────────────

void FsotaClient::setServerUrl(const char *url)
{
	strncpy(_serverUrl, url, sizeof(_serverUrl) - 1);
}

void FsotaClient::setHardwareModel(const char *m)
{
	strncpy(_hardwareModel, m, sizeof(_hardwareModel) - 1);
}

void FsotaClient::setCurrentFsVersion(const char *v)
{
	strncpy(_currentFsVersion, v, sizeof(_currentFsVersion) - 1);
}

void FsotaClient::setFsType(const char *t)
{
	strncpy(_fsType, t, sizeof(_fsType) - 1);
}

void FsotaClient::setAuthToken(const char *tok)
{
	strncpy(_authToken, tok, sizeof(_authToken) - 1);
}

void FsotaClient::setServerCACert(const char *ca)
{
	_caCert = ca;
}

void FsotaClient::setPartitionLabel(const char *lbl)
{
	strncpy(_partitionLabel, lbl, sizeof(_partitionLabel) - 1);
}

// ── Public API ────────────────────────────────────────────────────────────────

FsotaResult FsotaClient::checkForUpdate(FsotaManifest &manifest)
{
	if (WiFi.status() != WL_CONNECTED)
		return _setError(FsotaResult::ERR_WIFI, "WiFi not connected");

	if (_authToken[0] == '\0')
		return _setError(FsotaResult::ERR_TOKEN, "Auth token not set");

	WiFiClientSecure *wc = _createWifiClient();
	if (!wc)
		return _setError(FsotaResult::ERR_ALLOC, "Failed to allocate WiFiClientSecure");

	HTTPClient http;
	char url[512];
	snprintf(url, sizeof(url),
			 "%s" FOTA_API_PREFIX "/filesystem/check?hardware=%s&fs_version=%s&fs_type=%s",
			 _serverUrl, _hardwareModel, _currentFsVersion, _fsType);

	http.setConnectTimeout(FOTA_CONNECT_TIMEOUT_MS);
	http.setTimeout(FOTA_HTTP_TIMEOUT_MS);

	bool ok = http.begin(*wc, url);
	if (!ok)
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
		char msg[64];
		snprintf(msg, sizeof(msg), "Check HTTP %d", code);
		return _setError(FsotaResult::ERR_HTTP, msg);
	}

	String body = http.getString();
	http.end();
	_releaseWifiClient(wc);

	// Parse JSON
	JsonDocument doc;
	DeserializationError err = deserializeJson(doc, body);
	if (err)
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

	// Find partition
	const esp_partition_t *part = _findPartition();
	if (!part)
	{
		_emit("FAILED", manifest.version, _lastError);
		return _lastResult;
	}

	// Check size guard
	if (manifest.file_size > 0 && manifest.file_size > part->size)
	{
		char msg[80];
		snprintf(msg, sizeof(msg), "Image %u bytes > partition %u bytes",
				 (unsigned)manifest.file_size, (unsigned)part->size);
		_emit("FAILED", manifest.version, msg);
		return _setError(FsotaResult::ERR_SIZE, msg);
	}

	_emit("DOWNLOADING", manifest.version);

	FsotaResult wr = _streamToPartition(
		manifest.download_url,
		part,
		FSOTA_VERIFY_SHA256 ? manifest.hash : "",
		manifest.file_size);

	if (wr != FsotaResult::OK)
	{
		_emit("FAILED", manifest.version, _lastError);
		return wr;
	}

	_emit("COMPLETED", manifest.version);

#if FSOTA_REBOOT_ON_SUCCESS
	esp_restart();
#endif

	return (_lastResult = FsotaResult::OK);
}

// ── Private helpers ───────────────────────────────────────────────────────────

WiFiClientSecure *FsotaClient::_createWifiClient()
{
	auto *wc = new WiFiClientSecure();
	if (!wc)
		return nullptr;
	if (_caCert)
		wc->setCACert(_caCert);
	else
		wc->setInsecure(); // ⚠️ TLS peer verification disabled — only acceptable in dev
	return wc;
}

void FsotaClient::_releaseWifiClient(WiFiClientSecure *client)
{
	if (client)
		delete client;
}

void FsotaClient::_addAuthHeader(HTTPClient &http)
{
	if (_authToken[0] != '\0')
	{
		char hdr[530];
		snprintf(hdr, sizeof(hdr), "Bearer %s", _authToken);
		http.addHeader("Authorization", hdr);
	}
	http.addHeader("User-Agent", FSOTA_USER_AGENT);
}

void FsotaClient::_emit(const char *stage, const char *version, const char *error)
{
	if (_cb)
		_cb(stage, version, error ? error : "");
}

FsotaResult FsotaClient::_setError(FsotaResult code, const char *msg)
{
	strncpy(_lastError, msg ? msg : "", sizeof(_lastError) - 1);
	_lastError[sizeof(_lastError) - 1] = '\0';
	return (_lastResult = code);
}

const esp_partition_t *FsotaClient::_findPartition()
{
	// 1. Try by label first (most reliable)
	const esp_partition_t *part = esp_partition_find_first(
		ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, _partitionLabel);

	if (part)
		return part;

	// 2. Fall back to SPIFFS subtype (used by both SPIFFS and LittleFS on ESP32)
	part = esp_partition_find_first(
		ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);

	if (!part)
		_setError(FsotaResult::ERR_PARTITION,
				  "Filesystem partition not found — check partition table label");

	return part;
}

FsotaResult FsotaClient::_streamToPartition(
	const char *url,
	const esp_partition_t *partition,
	const char *expectedHash,
	uint32_t expectedSize)
{
	WiFiClientSecure *wc = _createWifiClient();
	if (!wc)
		return _setError(FsotaResult::ERR_ALLOC, "Failed to allocate WiFiClientSecure");

	HTTPClient http;
	http.setConnectTimeout(FOTA_CONNECT_TIMEOUT_MS);
	http.setTimeout((uint16_t)(FSOTA_DOWNLOAD_TIMEOUT_MS > 65535u ? 65535u : FSOTA_DOWNLOAD_TIMEOUT_MS)); // Clamp to uint16_t max
	http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);												  // Follow 302 from backend to Supabase

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
		char msg[64];
		snprintf(msg, sizeof(msg), "Download HTTP %d", code);
		return _setError(FsotaResult::ERR_HTTP, msg);
	}

	int contentLen = http.getSize();
	if (expectedSize > 0 && contentLen > 0 &&
		(uint32_t)contentLen != expectedSize)
	{
		http.end();
		_releaseWifiClient(wc);
		return _setError(FsotaResult::ERR_SIZE, "Content-Length does not match expected file_size");
	}

	uint32_t totalBytes = (contentLen > 0) ? (uint32_t)contentLen : expectedSize;
	if (totalBytes > 0 && totalBytes > partition->size)
	{
		http.end();
		_releaseWifiClient(wc);
		return _setError(FsotaResult::ERR_SIZE, "Image exceeds partition size");
	}

	// Allocate chunk buffer
	uint8_t *chunk = (uint8_t *)malloc(FSOTA_STREAM_CHUNK_SIZE);
	if (!chunk)
	{
		http.end();
		_releaseWifiClient(wc);
		return _setError(FsotaResult::ERR_ALLOC, "Chunk buffer allocation failed");
	}

	// Erase the entire partition before writing
	esp_err_t eraseErr = esp_partition_erase_range(partition, 0, partition->size);
	if (eraseErr != ESP_OK)
	{
		free(chunk);
		http.end();
		_releaseWifiClient(wc);
		return _setError(FsotaResult::ERR_FLASH, "Partition erase failed");
	}

	// SHA-256 context
	mbedtls_sha256_context sha256ctx;
	bool doHash = (expectedHash && expectedHash[0] != '\0');
	if (doHash)
		mbedtls_sha256_init(&sha256ctx);
#if defined(MBEDTLS_SHA256_ALT)
	if (doHash)
		mbedtls_sha256_starts(&sha256ctx, 0); // 0 = SHA-256 (not SHA-224)
#else
	if (doHash)
		mbedtls_sha256_starts_ret(&sha256ctx, 0);
#endif

	WiFiClient *stream = http.getStreamPtr();
	uint32_t offset = 0;
	uint32_t remaining = totalBytes; // 0 means unknown (chunked transfer)
	unsigned long dlStart = millis();
	FsotaResult result = FsotaResult::OK;

	_emit("INSTALLING", ""); // writing phase

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
			continue;
		}

		int toRead = min((int)FSOTA_STREAM_CHUNK_SIZE, avail);
		if (remaining > 0)
			toRead = min(toRead, (int)remaining);

		int got = stream->readBytes(chunk, toRead);
		if (got <= 0)
			break;

		// Pad to 4-byte boundary required by esp_partition_write
		int padded = (got + 3) & ~3;
		if (padded > got)
			memset(chunk + got, 0xFF, padded - got);

		// Write to partition
		esp_err_t wr = esp_partition_write(partition, offset, chunk, padded);
		if (wr != ESP_OK)
		{
			result = _setError(FsotaResult::ERR_FLASH, "Partition write failed");
			break;
		}

		if (doHash)
		{
#if defined(MBEDTLS_SHA256_ALT)
			mbedtls_sha256_update(&sha256ctx, chunk, got);
#else
			mbedtls_sha256_update_ret(&sha256ctx, chunk, got);
#endif
		}

		offset += got;
		if (remaining > 0)
			remaining -= got;

		yield(); // Feed watchdog on single-core builds
	}

	http.end();
	_releaseWifiClient(wc);
	free(chunk);

	if (result != FsotaResult::OK)
	{
		if (doHash)
			mbedtls_sha256_free(&sha256ctx);
		return result;
	}

	if (totalBytes > 0 && offset < totalBytes)
	{
		if (doHash)
			mbedtls_sha256_free(&sha256ctx);
		return _setError(FsotaResult::ERR_DOWNLOAD, "Incomplete download");
	}

	// Verify SHA-256
	if (doHash)
	{
		_emit("VERIFYING", "");
		uint8_t digest[32];
#if defined(MBEDTLS_SHA256_ALT)
		mbedtls_sha256_finish(&sha256ctx, digest);
#else
		mbedtls_sha256_finish_ret(&sha256ctx, digest);
#endif
		mbedtls_sha256_free(&sha256ctx);

		char hexDigest[65];
		for (int i = 0; i < 32; i++)
			snprintf(hexDigest + i * 2, 3, "%02x", digest[i]);
		hexDigest[64] = '\0';

		if (strcasecmp(hexDigest, expectedHash) != 0)
		{
			// Erase the partition to leave it in a clean (empty) state
			esp_partition_erase_range(partition, 0, partition->size);
			char msg[96];
			snprintf(msg, sizeof(msg), "SHA-256 mismatch: got %s", hexDigest);
			return _setError(FsotaResult::ERR_SHA256, msg);
		}
	}

	return (_lastResult = FsotaResult::OK);
}

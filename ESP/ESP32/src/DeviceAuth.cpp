/**
 * @file DeviceAuth.cpp
 * @brief Per-device authentication implementation for the FOTA ESP32 library.
 */

/*
 * DeviceAuth.cpp - Per-device authentication for the FOTA ESP32 library
 *
 * See DeviceAuth.h for the full API.
 */

#include "DeviceAuth.h"

#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <stdarg.h>
#include <time.h>

static const char *TAG = "DeviceAuth";

#define LOGV(fmt, ...) log_v("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGD(fmt, ...) log_d("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGI(fmt, ...) log_i("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGW(fmt, ...) log_w("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGE(fmt, ...) log_e("[%s] " fmt, TAG, ##__VA_ARGS__)

// ─── Constructor ──────────────────────────────────────────────────────────────

/** @brief Construct a DeviceAuth instance with compile-time defaults. */
DeviceAuth::DeviceAuth()
	: _caCert(nullptr), _tokenExpiry(0)
{
	LOGD("Constructing DeviceAuth with compile-time defaults");
	// Apply compile-time defaults so the sketch can call begin() with no runtime
	// configuration when all settings are provided via #define.
	strncpy(_serverUrl, DEVICE_AUTH_SERVER_URL, sizeof(_serverUrl) - 1);
	_serverUrl[sizeof(_serverUrl) - 1] = '\0';

	strncpy(_apiPrefix, DEVICE_AUTH_API_PREFIX, sizeof(_apiPrefix) - 1);
	_apiPrefix[sizeof(_apiPrefix) - 1] = '\0';

	strncpy(_deviceId, DEVICE_AUTH_DEVICE_ID, sizeof(_deviceId) - 1);
	_deviceId[sizeof(_deviceId) - 1] = '\0';

	strncpy(_deviceSecret, DEVICE_AUTH_DEVICE_SECRET, sizeof(_deviceSecret) - 1);
	_deviceSecret[sizeof(_deviceSecret) - 1] = '\0';

	_token[0] = '\0';
	_lastError[0] = '\0';
}

// ─── Configuration ────────────────────────────────────────────────────────────

/** @brief Set backend base URL used for device authentication. */
void DeviceAuth::setServerUrl(const char *url)
{
	strncpy(_serverUrl, url, sizeof(_serverUrl) - 1);
	_serverUrl[sizeof(_serverUrl) - 1] = '\0';
}

/** @brief Set API prefix used for authentication endpoints. */
void DeviceAuth::setApiPrefix(const char *prefix)
{
	strncpy(_apiPrefix, prefix, sizeof(_apiPrefix) - 1);
	_apiPrefix[sizeof(_apiPrefix) - 1] = '\0';
}

/** @brief Set dashboard device identifier used during authentication. */
void DeviceAuth::setDeviceId(const char *deviceId)
{
	strncpy(_deviceId, deviceId, sizeof(_deviceId) - 1);
	_deviceId[sizeof(_deviceId) - 1] = '\0';
}

/** @brief Set device secret used in authenticate() requests. */
void DeviceAuth::setDeviceSecret(const char *secret)
{
	strncpy(_deviceSecret, secret, sizeof(_deviceSecret) - 1);
	_deviceSecret[sizeof(_deviceSecret) - 1] = '\0';
}

/** @brief Set root CA certificate used for TLS verification. */
void DeviceAuth::setCACert(const char *pem)
{
	_caCert = pem;
}

// ─── Core API ─────────────────────────────────────────────────────────────────

/** @brief Ensure a valid device JWT is available in memory. */
bool DeviceAuth::ensureValid()
{
	LOGI("Ensuring valid device auth token");
	// Fast path: in-memory token is still good.
	if (isTokenValid())
	{
		LOGD("Token already valid in memory");
		return true;
	}

	// Try loading from NVS first.
	if (loadFromNVS() && isTokenValid())
	{
		LOGD("Loaded valid token from NVS");
		return true;
	}

	// Token is absent or expired — authenticate to get a fresh one.
	return authenticate();
}

/** @brief Authenticate device credentials and cache JWT token in NVS. */
bool DeviceAuth::authenticate()
{
	LOGI("Device authentication started");
	if (_serverUrl[0] == '\0')
	{
		_setError("serverUrl not set");
		return false;
	}
	if (_deviceId[0] == '\0')
	{
		_setError("deviceId not set");
		return false;
	}
	if (_deviceSecret[0] == '\0')
	{
		_setError("deviceSecret not set");
		return false;
	}

	// Build the endpoint URL: <serverUrl><apiPrefix>/devices/authenticate
	char url[384];
	snprintf(url, sizeof(url), "%s%s/devices/authenticate", _serverUrl, _apiPrefix);

	// Serialize request body.
	JsonDocument reqDoc;
	reqDoc["device_id"] = _deviceId;
	reqDoc["device_secret"] = _deviceSecret;

	char body[512];
	serializeJson(reqDoc, body, sizeof(body));

	// Configure TLS.
	WiFiClientSecure wifiClient;
	if (_caCert)
	{
		wifiClient.setCACert(_caCert);
	}
	else
	{
		wifiClient.setInsecure(); // Development / self-signed only.
	}

	HTTPClient http;
	http.setTimeout(FOTA_CONNECT_TIMEOUT_MS);
	http.setConnectTimeout(FOTA_CONNECT_TIMEOUT_MS);
	http.addHeader("Content-Type", "application/json");
	http.setUserAgent(FOTA_USER_AGENT);

	if (!http.begin(wifiClient, url))
	{
		_setError("HTTP begin failed for: %s", url);
		return false;
	}

	int httpCode = http.POST((uint8_t *)body, strlen(body));
	LOGD("Authenticate HTTP response code=%d", httpCode);

	if (httpCode != HTTP_CODE_OK)
	{
		String errBody = http.getString();
		http.end();
		_setError("Auth HTTP %d: %s", httpCode, errBody.substring(0, 64).c_str());
		return false;
	}

	// Parse response.
	String respBody = http.getString();
	http.end();

	JsonDocument respDoc;
	DeserializationError err = deserializeJson(respDoc, respBody);
	if (err)
	{
		_setError("JSON parse error: %s", err.c_str());
		return false;
	}

	const char *token = respDoc["token"] | "";
	if (token[0] == '\0')
	{
		_setError("Response missing 'token' field");
		return false;
	}

	strncpy(_token, token, sizeof(_token) - 1);
	_token[sizeof(_token) - 1] = '\0';
	LOGV("Token received with length=%u", (unsigned)strlen(_token));

	if (!_parseExpiry())
	{
		// Non-fatal: cache the token without a known expiry. isTokenValid() will
		// skip the expiry check if _tokenExpiry is 0.
		_tokenExpiry = 0;
	}

	saveToNVS();
	LOGI("Device authentication succeeded and token cached");
	_lastError[0] = '\0';
	return true;
}

// ─── Token accessors ──────────────────────────────────────────────────────────

/** @brief Check whether in-memory JWT token is currently valid. */
bool DeviceAuth::isTokenValid() const
{
	if (_token[0] == '\0')
		return false;

	// If expiry is unknown, assume valid (we just received the token).
	if (_tokenExpiry == 0)
		return true;

	// If the device clock hasn't been set yet (time < 2020-01-01), skip check.
	time_t now = time(nullptr);
	if (now < 1577836800UL)
		return true;

	return (uint32_t)now + DEVICE_AUTH_REAUTH_MARGIN < _tokenExpiry;
}

// ─── NVS persistence ──────────────────────────────────────────────────────────

/** @brief Load cached token and expiry from NVS into memory. */
bool DeviceAuth::loadFromNVS()
{
	LOGD("Loading token from NVS");
	Preferences prefs;
	if (!prefs.begin(DEVICE_AUTH_NVS_NS, /*readOnly=*/true))
		return false;

	String saved = prefs.getString("token", "");
	if (saved.length() == 0)
	{
		prefs.end();
		return false;
	}

	strncpy(_token, saved.c_str(), sizeof(_token) - 1);
	_token[sizeof(_token) - 1] = '\0';
	_tokenExpiry = prefs.getUInt("expiry", 0);
	LOGV("Loaded token from NVS with expiry=%u", (unsigned)_tokenExpiry);

	prefs.end();
	return true;
}

/** @brief Persist current token and expiry timestamp to NVS. */
void DeviceAuth::saveToNVS()
{
	LOGD("Saving token to NVS");
	Preferences prefs;
	if (!prefs.begin(DEVICE_AUTH_NVS_NS, /*readOnly=*/false))
		return;

	prefs.putString("token", _token);
	prefs.putUInt("expiry", _tokenExpiry);
	prefs.end();
}

/** @brief Clear cached authentication token state from NVS and memory. */
void DeviceAuth::clearNVS()
{
	LOGW("Clearing auth token from NVS");
	Preferences prefs;
	if (!prefs.begin(DEVICE_AUTH_NVS_NS, /*readOnly=*/false))
		return;
	prefs.clear();
	prefs.end();

	_token[0] = '\0';
	_tokenExpiry = 0;
}

// ─── Private helpers ──────────────────────────────────────────────────────────

/** @brief Parse JWT exp claim from token payload and cache it in _tokenExpiry. */
bool DeviceAuth::_parseExpiry()
{
	LOGD("Parsing JWT expiry claim");
	// JWT structure: <base64url-header>.<base64url-payload>.<signature>
	// We decode only the payload segment (second part).

	const char *dot1 = strchr(_token, '.');
	if (!dot1)
		return false;

	const char *segStart = dot1 + 1;
	const char *dot2 = strchr(segStart, '.');
	if (!dot2)
		return false;

	int segLen = (int)(dot2 - segStart);
	if (segLen <= 0 || segLen > 1024)
		return false;

	// Convert base64url to standard base64 and add padding.
	char b64[1030];
	int bi = 0;
	for (int i = 0; i < segLen && bi < (int)sizeof(b64) - 4; i++)
	{
		char c = segStart[i];
		if (c == '-')
			c = '+';
		else if (c == '_')
			c = '/';
		b64[bi++] = c;
	}
	while (bi % 4 != 0 && bi < (int)sizeof(b64) - 1)
		b64[bi++] = '=';
	b64[bi] = '\0';

	// Decode using mbedTLS (always present on ESP32 via ESP-IDF).
	uint8_t decoded[768];
	size_t decodedLen = 0;
	if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decodedLen,
							  (const uint8_t *)b64, (size_t)bi) != 0)
	{
		return false;
	}
	decoded[decodedLen] = '\0';

	// Locate "exp": in the JSON payload.
	const char *expPtr = strstr((char *)decoded, "\"exp\":");
	if (!expPtr)
		return false;

	expPtr += 6; // skip  "exp":
	while (*expPtr == ' ')
		expPtr++;

	unsigned long val = strtoul(expPtr, nullptr, 10);
	if (val == 0)
		return false;

	_tokenExpiry = (uint32_t)val;
	LOGV("Parsed JWT exp=%u", (unsigned)_tokenExpiry);
	return true;
}

/** @brief Format and store last internal error message. */
void DeviceAuth::_setError(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vsnprintf(_lastError, sizeof(_lastError), fmt, args);
	va_end(args);
	LOGE("%s", _lastError);
}

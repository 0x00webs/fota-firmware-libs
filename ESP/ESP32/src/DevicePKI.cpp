/**
 * @file DevicePKI.cpp
 * @brief ESP32 PKI provisioning implementation for key, CSR, and certificate flow.
 */

/*
 * DevicePKI.cpp - ESP32 PKI certificate provisioning implementation
 *
 * Uses mbedTLS (bundled with arduino-esp32 / ESP-IDF) for:
 *   - ECDSA P-256 key pair generation
 *   - PKCS#10 CSR creation and signing
 *
 * Credentials are persisted via the Arduino Preferences library (NVS).
 * Network operations use WiFiClientSecure + HTTPClient.
 */

#include "DevicePKI.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

// mbedTLS headers
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_csr.h>

static const char *TAG = "DevicePKI";

#define LOGV(fmt, ...) log_v("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGD(fmt, ...) log_d("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGI(fmt, ...) log_i("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGW(fmt, ...) log_w("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGE(fmt, ...) log_e("[%s] " fmt, TAG, ##__VA_ARGS__)

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Convert PKIResult values to stable log-friendly text. */
const char *pkiResultStr(PKIResult r)
{
	switch (r)
	{
	case PKIResult::OK:
		return "OK";
	case PKIResult::ERR_KEYGEN:
		return "ERR_KEYGEN";
	case PKIResult::ERR_CSR:
		return "ERR_CSR";
	case PKIResult::ERR_NETWORK:
		return "ERR_NETWORK";
	case PKIResult::ERR_SERVER:
		return "ERR_SERVER";
	case PKIResult::ERR_NVS:
		return "ERR_NVS";
	case PKIResult::ERR_NO_CERT:
		return "ERR_NO_CERT";
	default:
		return "ERR_UNKNOWN";
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Construct an empty DevicePKI cache instance. */
DevicePKI::DevicePKI()
	: _keyLoaded(false), _certLoaded(false)
{
	_keyPem[0] = '\0';
	_certPem[0] = '\0';
}

/** @brief Destroy DevicePKI instance. */
DevicePKI::~DevicePKI()
{
}

// ─────────────────────────────────────────────────────────────────────────────
// NVS helpers
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Write a string credential field to NVS. */
PKIResult DevicePKI::_nvsWrite(const char *key, const char *value)
{
	Preferences prefs;
	if (!prefs.begin(DEVICE_PKI_NVS_NS, false))
	{
		LOGE("NVS open (write) failed for namespace: %s", DEVICE_PKI_NVS_NS);
		return PKIResult::ERR_NVS;
	}
	size_t written = prefs.putString(key, value);
	prefs.end();
	if (written == 0)
	{
		LOGE("NVS putString failed for key: %s", key);
		return PKIResult::ERR_NVS;
	}
	return PKIResult::OK;
}

/** @brief Load persisted key/certificate credentials from NVS cache. */
PKIResult DevicePKI::_loadFromNVS()
{
	Preferences prefs;
	if (!prefs.begin(DEVICE_PKI_NVS_NS, true /* read-only */))
	{
		LOGE("NVS open (read) failed for namespace: %s", DEVICE_PKI_NVS_NS);
		return PKIResult::ERR_NVS;
	}

	String keyStr = prefs.getString("key_pem", "");
	String certStr = prefs.getString("cert_pem", "");
	prefs.end();

	if (keyStr.length() > 0)
	{
		strncpy(_keyPem, keyStr.c_str(), sizeof(_keyPem) - 1);
		_keyPem[sizeof(_keyPem) - 1] = '\0';
		_keyLoaded = true;
	}
	if (certStr.length() > 0)
	{
		strncpy(_certPem, certStr.c_str(), sizeof(_certPem) - 1);
		_certPem[sizeof(_certPem) - 1] = '\0';
		_certLoaded = true;
	}
	return PKIResult::OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// generateKeyPair
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Generate or reuse device ECDSA P-256 keypair and store private key in NVS. */
PKIResult DevicePKI::generateKeyPair(bool forceRegenerate)
{
	LOGI("generateKeyPair started (force=%d)", forceRegenerate ? 1 : 0);
	// Check existing key in NVS unless force-regenerating
	if (!forceRegenerate)
	{
		Preferences prefs;
		if (prefs.begin(DEVICE_PKI_NVS_NS, true))
		{
			bool exists = prefs.isKey("key_pem");
			prefs.end();
			if (exists)
			{
				LOGI("Key already exists in NVS, skipping generation");
				return PKIResult::OK;
			}
		}
	}

	// Initialise mbedTLS entropy + DRBG
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctrDrbg;
	mbedtls_pk_context pk;

	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&ctrDrbg);
	mbedtls_pk_init(&pk);

	int ret = 0;
	const char *persStr = "fota_device_pki";

	ret = mbedtls_ctr_drbg_seed(&ctrDrbg, mbedtls_entropy_func, &entropy,
								(const unsigned char *)persStr, strlen(persStr));
	if (ret != 0)
	{
		char errBuf[128];
		mbedtls_strerror(ret, errBuf, sizeof(errBuf));
		LOGE("DRBG seed failed: %s", errBuf);
		mbedtls_pk_free(&pk);
		mbedtls_ctr_drbg_free(&ctrDrbg);
		mbedtls_entropy_free(&entropy);
		return PKIResult::ERR_KEYGEN;
	}

	// Setup an EC key context and generate seal SECP256R1 key pair
	ret = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
	if (ret != 0)
	{
		LOGE("pk_setup failed: -0x%04X", (unsigned int)-ret);
		mbedtls_pk_free(&pk);
		mbedtls_ctr_drbg_free(&ctrDrbg);
		mbedtls_entropy_free(&entropy);
		return PKIResult::ERR_KEYGEN;
	}

	ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,
							  mbedtls_pk_ec(pk),
							  mbedtls_ctr_drbg_random,
							  &ctrDrbg);
	if (ret != 0)
	{
		char errBuf[128];
		mbedtls_strerror(ret, errBuf, sizeof(errBuf));
		LOGE("ecp_gen_key failed: %s", errBuf);
		mbedtls_pk_free(&pk);
		mbedtls_ctr_drbg_free(&ctrDrbg);
		mbedtls_entropy_free(&entropy);
		return PKIResult::ERR_KEYGEN;
	}

	// Export private key as PEM
	unsigned char pemBuf[2048] = {0};
	ret = mbedtls_pk_write_key_pem(&pk, pemBuf, sizeof(pemBuf));
	if (ret != 0)
	{
		char errBuf[128];
		mbedtls_strerror(ret, errBuf, sizeof(errBuf));
		LOGE("pk_write_key_pem failed: %s", errBuf);
		mbedtls_pk_free(&pk);
		mbedtls_ctr_drbg_free(&ctrDrbg);
		mbedtls_entropy_free(&entropy);
		return PKIResult::ERR_KEYGEN;
	}

	mbedtls_pk_free(&pk);
	mbedtls_ctr_drbg_free(&ctrDrbg);
	mbedtls_entropy_free(&entropy);

	// Persist to NVS
	PKIResult nvsResult = _nvsWrite("key_pem", (const char *)pemBuf);
	if (nvsResult != PKIResult::OK)
		return nvsResult;

	// Cache in member buffer
	strncpy(_keyPem, (const char *)pemBuf, sizeof(_keyPem) - 1);
	_keyPem[sizeof(_keyPem) - 1] = '\0';
	_keyLoaded = true;

	LOGI("ECDSA P-256 key pair generated and stored in NVS");
	return PKIResult::OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// generateCSR
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Build a PEM PKCS#10 CSR for the provided device identifier. */
PKIResult DevicePKI::generateCSR(const char *deviceId, char *outPem, size_t outLen)
{
	LOGI("generateCSR started for deviceId=%s", deviceId ? deviceId : "<null>");
	if (!_keyLoaded)
	{
		PKIResult r = _loadFromNVS();
		if (r != PKIResult::OK || !_keyLoaded)
		{
			LOGE("No private key available — call generateKeyPair() first");
			return PKIResult::ERR_CSR;
		}
	}

	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctrDrbg;
	mbedtls_pk_context pk;
	mbedtls_x509write_csr csr;

	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&ctrDrbg);
	mbedtls_pk_init(&pk);
	mbedtls_x509write_csr_init(&csr);

	int ret = 0;
	const char *persStr = "fota_device_csr";

	ret = mbedtls_ctr_drbg_seed(&ctrDrbg, mbedtls_entropy_func, &entropy,
								(const unsigned char *)persStr, strlen(persStr));
	if (ret != 0)
	{
		LOGE("DRBG seed failed: -0x%04X", (unsigned int)-ret);
		goto csr_cleanup;
	}

	// Parse the stored private key
	// mbedTLS 2.x (ESP32 Arduino): 5-arg form without f_rng
	ret = mbedtls_pk_parse_key(
		&pk,
		(const unsigned char *)_keyPem,
		strlen(_keyPem) + 1, // include NUL terminator
		nullptr, 0);
	if (ret != 0)
	{
		char errBuf[128];
		mbedtls_strerror(ret, errBuf, sizeof(errBuf));
		LOGE("pk_parse_key failed: %s", errBuf);
		goto csr_cleanup;
	}

	// Build CSR
	mbedtls_x509write_csr_set_key(&csr, &pk);
	mbedtls_x509write_csr_set_md_alg(&csr, MBEDTLS_MD_SHA256);

	{
		// Subject: CN=<deviceId>
		char subjectBuf[128];
		snprintf(subjectBuf, sizeof(subjectBuf), "CN=%s", deviceId);
		ret = mbedtls_x509write_csr_set_subject_name(&csr, subjectBuf);
		if (ret != 0)
		{
			char errBuf[128];
			mbedtls_strerror(ret, errBuf, sizeof(errBuf));
			LOGE("csr_set_subject_name failed: %s", errBuf);
			goto csr_cleanup;
		}
	}

	// Export as PEM
	ret = mbedtls_x509write_csr_pem(&csr,
									(unsigned char *)outPem, outLen,
									mbedtls_ctr_drbg_random, &ctrDrbg);
	if (ret != 0)
	{
		char errBuf[128];
		mbedtls_strerror(ret, errBuf, sizeof(errBuf));
		LOGE("csr_pem failed: %s", errBuf);
		goto csr_cleanup;
	}

	LOGI("CSR generated for device: %s", deviceId);
	LOGV("CSR output length=%u", (unsigned)strlen(outPem));

csr_cleanup:
	mbedtls_x509write_csr_free(&csr);
	mbedtls_pk_free(&pk);
	mbedtls_ctr_drbg_free(&ctrDrbg);
	mbedtls_entropy_free(&entropy);
	return (ret == 0) ? PKIResult::OK : PKIResult::ERR_CSR;
}

// ─────────────────────────────────────────────────────────────────────────────
// submitCSR
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Submit CSR to backend and persist issued certificate in NVS. */
PKIResult DevicePKI::submitCSR(const char *serverUrl,
							   const char *deviceDbId,
							   const char *operatorJwt,
							   const char *csrPem,
							   const char *caCert)
{
	char url[512];
	snprintf(url, sizeof(url), "%s/api/v1/pki/devices/%s/csr", serverUrl, deviceDbId);
	LOGI("submitCSR started for deviceDbId=%s", deviceDbId ? deviceDbId : "<null>");

	// Escape newlines in PEM for JSON embedding
	// The CSR PEM uses \n line endings; replace with \\n for the JSON body
	String csrStr(csrPem);
	csrStr.replace("\n", "\\n");

	// Field name must match the backend's SubmitCsrDto.csr property.
	String body = "{\"csr\":\"" + csrStr + "\"}";

	WiFiClientSecure wifiClient;
	if (caCert)
		wifiClient.setCACert(caCert);
	else
		wifiClient.setInsecure();

	HTTPClient http;
	if (!http.begin(wifiClient, url))
	{
		LOGE("HTTPClient.begin() failed for URL: %s", url);
		return PKIResult::ERR_NETWORK;
	}

	char authHeader[1024];
	snprintf(authHeader, sizeof(authHeader), "Bearer %s", operatorJwt);
	http.addHeader("Authorization", authHeader);
	http.addHeader("Content-Type", "application/json");

	int httpCode = http.POST(body);
	LOGD("submitCSR HTTP response code=%d", httpCode);
	if (httpCode <= 0)
	{
		LOGE("POST failed, error: %s", http.errorToString(httpCode).c_str());
		http.end();
		return PKIResult::ERR_NETWORK;
	}

	if (httpCode < 200 || httpCode >= 300)
	{
		LOGE("Server rejected CSR — HTTP %d: %s", httpCode, http.getString().c_str());
		http.end();
		return PKIResult::ERR_SERVER;
	}

	// Parse response: { "certificate": "-----BEGIN CERTIFICATE-----\n..." }
	String resp = http.getString();
	http.end();

	// Simple extraction: find "certificate":"..." value
	int startIdx = resp.indexOf("\"certificate\":\"");
	if (startIdx < 0)
	{
		LOGE("Response missing 'certificate' field: %s", resp.c_str());
		return PKIResult::ERR_SERVER;
	}
	startIdx += strlen("\"certificate\":\"");
	int endIdx = resp.indexOf("\"", startIdx);
	if (endIdx < 0)
	{
		LOGE("Malformed certificate field in response");
		return PKIResult::ERR_SERVER;
	}

	String certEscaped = resp.substring(startIdx, endIdx);
	// Unescape \\n → \n within the PEM
	certEscaped.replace("\\n", "\n");

	LOGI("Certificate received from server (%d bytes)", certEscaped.length());

	return storeCertificate(certEscaped.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// storeCertificate
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Persist an issued PEM certificate to NVS and in-memory cache. */
PKIResult DevicePKI::storeCertificate(const char *certPem)
{
	PKIResult r = _nvsWrite("cert_pem", certPem);
	if (r != PKIResult::OK)
		return r;

	strncpy(_certPem, certPem, sizeof(_certPem) - 1);
	_certPem[sizeof(_certPem) - 1] = '\0';
	_certLoaded = true;

	LOGI("Certificate stored in NVS");
	return PKIResult::OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// provision
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Execute full provisioning flow: keypair, CSR, submit, and certificate storage. */
PKIResult DevicePKI::provision(const char *serverUrl,
							   const char *deviceDbId,
							   const char *operatorJwt,
							   const char *caCert,
							   bool forceReprovision)
{
	LOGI("Device provisioning started (force=%d)", forceReprovision ? 1 : 0);
	if (!forceReprovision && hasValidCert())
	{
		LOGI("Device already provisioned, skipping");
		return PKIResult::OK;
	}

	// Step 1 — generate/reuse key
	PKIResult r = generateKeyPair(forceReprovision);
	if (r != PKIResult::OK)
	{
		LOGE("generateKeyPair failed: %s", pkiResultStr(r));
		return r;
	}

	// Step 2 — build CSR
	char csrPem[2048] = {0};
	r = generateCSR(deviceDbId, csrPem, sizeof(csrPem));
	if (r != PKIResult::OK)
	{
		LOGE("generateCSR failed: %s", pkiResultStr(r));
		return r;
	}

	// Step 3 — submit to server
	r = submitCSR(serverUrl, deviceDbId, operatorJwt, csrPem, caCert);
	if (r != PKIResult::OK)
	{
		LOGE("submitCSR failed: %s", pkiResultStr(r));
		return r;
	}

	LOGI("Device provisioning complete");
	return PKIResult::OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// hasValidCert
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Check if a certificate is present in NVS storage. */
bool DevicePKI::hasValidCert() const
{
	Preferences prefs;
	if (!prefs.begin(DEVICE_PKI_NVS_NS, true))
		return false;
	bool exists = prefs.isKey("cert_pem");
	prefs.end();
	return exists;
}

// ─────────────────────────────────────────────────────────────────────────────
// clearCredentials
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Remove persisted PKI credentials from NVS and reset in-memory cache. */
void DevicePKI::clearCredentials()
{
	Preferences prefs;
	if (prefs.begin(DEVICE_PKI_NVS_NS, false))
	{
		prefs.remove("key_pem");
		prefs.remove("cert_pem");
		prefs.end();
	}
	_keyPem[0] = '\0';
	_certPem[0] = '\0';
	_keyLoaded = false;
	_certLoaded = false;
	LOGI("PKI credentials cleared from NVS");
}

// ─────────────────────────────────────────────────────────────────────────────
// applyTo
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Apply loaded certificate and key material to a FotaClient instance. */
PKIResult DevicePKI::applyTo(FotaClient &fota)
{
	if (!_keyLoaded || !_certLoaded)
	{
		PKIResult r = _loadFromNVS();
		if (r != PKIResult::OK)
			return r;
	}

	if (!_keyLoaded || !_certLoaded)
	{
		LOGE("applyTo: credentials not available in NVS");
		return PKIResult::ERR_NO_CERT;
	}

	fota.setClientCert(_certPem);
	fota.setClientKey(_keyPem);
	LOGI("mTLS credentials applied to FotaClient");
	return PKIResult::OK;
}

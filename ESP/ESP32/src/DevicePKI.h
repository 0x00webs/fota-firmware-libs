#pragma once

/**
 * @file DevicePKI.h
 * @brief Device keypair, CSR, and certificate provisioning helpers for ESP32.
 */

/*
 * DevicePKI.h - ESP32 PKI certificate provisioning for the FOTA platform
 *
 * Handles the full ECDSA P-256 key generation + CSR signing flow, NVS-backed
 * credential persistence, and mTLS wiring into FotaClient.
 *
 * Typical usage:
 *
 *   DevicePKI pki;
 *
 *   // One-shot: generate key, build CSR, upload to server, store cert.
 *   // deviceDbId is the UUID from the FOTA platform device registry.
 *   // operatorJwt is a short-lived admin/operator Bearer token.
 *   if (pki.provision(FOTA_SERVER_URL, DEVICE_DB_ID, OPERATOR_JWT) == PKIResult::OK) {
 *       pki.applyTo(fota);   // inject cert + key into FotaClient
 *   }
 *
 *   // On subsequent boots:
 *   if (pki.hasValidCert()) {
 *       pki.applyTo(fota);
 *   }
 *
 * NVS namespace: "fota_pki"  (override with DEVICE_PKI_NVS_NS before include)
 * NVS keys     : "key_pem", "cert_pem"
 *
 * Dependencies:
 *   - mbedTLS (bundled with arduino-esp32 / ESP-IDF)
 *   - HTTPClient + WiFiClientSecure (bundled with arduino-esp32)
 *   - Preferences (bundled with arduino-esp32)
 *   - FotaClient.h (this library)
 */

#ifndef DEVICE_PKI_NVS_NS
#define DEVICE_PKI_NVS_NS "fota_pki"
#endif

#include "FotaClient.h"

#include <Arduino.h>
#include <Preferences.h>

// ── Result codes ─────────────────────────────────────────────────────────────

enum class PKIResult : int8_t
{
	OK            =  0,  ///< Operation succeeded
	ERR_KEYGEN    = -1,  ///< mbedTLS key generation failed
	ERR_CSR       = -2,  ///< CSR generation failed
	ERR_NETWORK   = -3,  ///< HTTP transport error
	ERR_SERVER    = -4,  ///< Server rejected the CSR (4xx/5xx)
	ERR_NVS       = -5,  ///< NVS read/write failure
	ERR_NO_CERT   = -6,  ///< No certificate available in NVS
};

/**
 * @brief Convert a PKIResult to a human-readable C-string.
 * @param r PKIResult code.
 * @return Constant string representation of the result code.
 */
const char *pkiResultStr(PKIResult r);

// ── DevicePKI ─────────────────────────────────────────────────────────────────

/**
 * @class DevicePKI
 * @brief Handles key generation, CSR submission, and mTLS credential wiring.
 */
class DevicePKI
{
public:
	/** @brief Construct a DevicePKI instance. */
	DevicePKI();
	/** @brief Destroy the DevicePKI instance. */
	~DevicePKI();

	// ── Provisioning ─────────────────────────────────────────────────────────

	/**
	 * @brief Generate a new ECDSA P-256 key pair and store the private key in NVS.
	 *
	 * Safe to call on every boot — if a key already exists in NVS this call
	 * is a no-op and returns PKIResult::OK immediately.
	 * Pass forceRegenerate=true to overwrite an existing key (e.g. after
	 * certificate revocation).
	 *
	 * @param forceRegenerate  Overwrite existing NVS key (default: false).
	 * @note Emits info/debug logs for key generation workflow and crypto failures.
	 * @return PKIResult::OK on success (or key already exists).
	 */
	PKIResult generateKeyPair(bool forceRegenerate = false);

	/**
	 * @brief Build a PKCS#10 CSR (PEM-encoded) signed with the stored private key.
	 *
	 * The CSR subject contains only CN=<deviceId>.
	 * generateKeyPair() must be called (and succeed) before this.
	 *
	 * @param deviceId  The device business-key (e.g. "esp32-room-sensor-01").
	 * @param outPem    Buffer that receives the PEM CSR string (NUL-terminated).
	 * @param outLen    Size of outPem in bytes. Minimum: 1024.
	 * @return PKIResult::OK on success.
	 */
	PKIResult generateCSR(const char *deviceId, char *outPem, size_t outLen);

	/**
	 * @brief POST a PEM CSR to the FOTA server and store the signed certificate.
	 *
	 * Endpoint: POST <serverUrl>/api/v1/pki/devices/<deviceDbId>/csr
	 * Auth: Bearer <operatorJwt>
	 * Body: { "csr_pem": "<pem>" }
	 *
	 * On success, the signed certificate is persisted to NVS.
	 *
	 * @param serverUrl   FOTA backend base URL.
	 * @param deviceDbId  UUID of the device in the FOTA platform database.
	 * @param operatorJwt Admin or OPERATOR JWT for the signing request.
	 * @param csrPem      PEM string from generateCSR().
	 * @param caCert      PEM CA cert for TLS server verification (or nullptr to skip).
	 * @note Emits debug logs for HTTP response codes and error logs for server rejections.
	 * @return PKIResult::OK on success.
	 */
	PKIResult submitCSR(const char *serverUrl,
						const char *deviceDbId,
						const char *operatorJwt,
						const char *csrPem,
						const char *caCert = nullptr);

	/**
	 * @brief Persist a PEM certificate string to NVS.
	 *
	 * Called internally by submitCSR(); exposed publicly so certificates
	 * provisioned out-of-band (e.g. pre-flashed) can also be stored.
	 *
	 * @param certPem  NUL-terminated PEM certificate string.
	 * @return PKIResult::OK on success.
	 */
	PKIResult storeCertificate(const char *certPem);

	/**
	 * @brief Run full provisioning flow in one call.
	 *
	 * Steps performed:
	 *   1. generateKeyPair()  — no-op if key already exists
	 *   2. generateCSR()      — using deviceId derived from deviceDbId
	 *   3. submitCSR()        — POST to server, store result
	 *
	 * Skips steps 2+3 if hasValidCert() returns true (already provisioned).
	 * Pass forceReprovision=true to re-run even if a cert exists (rotation).
	 *
	 * @param serverUrl        FOTA backend base URL.
	 * @param deviceDbId       UUID of the device in the FOTA platform database.
	 * @param operatorJwt      OPERATOR/ADMIN JWT for signing authorisation.
	 * @param caCert           TLS CA cert for server verification (or nullptr).
	 * @param forceReprovision Re-provision even if a cert already exists.
	 * @note Emits info logs for each provisioning step and error logs on failures.
	 * @return PKIResult::OK on success.
	 */
	PKIResult provision(const char *serverUrl,
						const char *deviceDbId,
						const char *operatorJwt,
						const char *caCert = nullptr,
						bool forceReprovision = false);

	// ── Certificate lifecycle ──────────────────────────────────────────────

	/**
	 * @brief Check whether a certificate has been stored in NVS.
	 *
	 * Does NOT validate expiry — the server enforces cert validity.
	 *
	 * @return true if a cert_pem key exists in NVS.
	 */
	bool hasValidCert() const;

	/**
	 * @brief Remove both the private key and certificate from NVS.
	 *
	 * Call this when the server revokes the device certificate so that
	 * a fresh provisioning cycle will be triggered on the next boot.
	 * @return void
	 */
	void clearCredentials();

	// ── FotaClient integration ─────────────────────────────────────────────

	/**
	 * @brief Inject the stored private key and certificate into a FotaClient.
	 *
	 * After this call, FotaClient will present the client certificate during
	 * the TLS handshake for every API request (mTLS).
	 *
	 * The DevicePKI instance must remain alive as long as the FotaClient is
	 * used (the pointers are held by reference, not copied).
	 *
	 * @param fota  FotaClient instance to configure.
	 * @return PKIResult::OK on success, PKIResult::ERR_NO_CERT if credentials
	 *         are not available in NVS.
	 */
	PKIResult applyTo(FotaClient &fota);

private:
	// NVS-backed credential buffers (loaded on demand)
	char  _keyPem[2048];   ///< PEM private key (loaded from NVS)
	char  _certPem[2048];  ///< PEM certificate (loaded from NVS)
	bool  _keyLoaded;      ///< True after successful NVS key load
	bool  _certLoaded;     ///< True after successful NVS cert load

	/** Open NVS namespace and load key + cert into member buffers. */
	PKIResult _loadFromNVS();

	/** Write a string value to NVS under the PKI namespace. */
	PKIResult _nvsWrite(const char *key, const char *value);
};

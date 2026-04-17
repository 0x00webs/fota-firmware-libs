#pragma once

/**
 * @file FotaVerify.h
 * @brief Low-level SHA-256 and digital-signature verification helpers.
 */

/*
 * FotaVerify.h - SHA-256 integrity and digital-signature verification
 *
 * Uses mbedTLS (bundled with ESP-IDF / Arduino-ESP32) exclusively.
 * No external crypto libraries required.
 *
 * Supported signature algorithms (matching the backend signature_algorithm field):
 *   "ECDSA_P256"  — ECDSA over NIST P-256 (prime256v1), SHA-256
 *   "RSA_SHA256"  — RSA-PSS with SHA-256
 *   "ED25519"     — Edwards-curve DSA (Ed25519), mbedTLS 3.x / ESP-IDF ≥ 5.0
 *
 * Signature byte layout (from the backend):
 *   The backend calls crypto.sign() / createSign('SHA256') over the *raw*
 *   32-byte SHA-256 digest (i.e. Buffer.from(sha256HexString, 'hex')).
 *   The result is base64-encoded before placing in the JSON response.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Verify the SHA-256 hash of a memory buffer.
 *
 * @param data         Pointer to firmware binary in RAM.
 * @param len          Length of data in bytes.
 * @param expected_hex Expected SHA-256 as a 64-character lowercase hex string.
 * @note Emits verbose/debug/error logs describing hash verification outcomes.
 * @return  0  on success (hash matches).
 *         -1  on mismatch or error.
 */
int fotaVerifySha256(const uint8_t *data, size_t len, const char *expected_hex);

/**
 * @brief Compare an already-computed SHA-256 digest against expected hex.
 * @param hash32 Pointer to 32-byte SHA-256 digest.
 * @param expected_hex Expected SHA-256 as a 64-character lowercase hex string.
 * @return 0 on success (hash matches), -1 on mismatch or error.
 */
int fotaVerifySha256Digest(const uint8_t hash32[32], const char *expected_hex);

/**
 * @brief Verify the digital signature over a 32-byte SHA-256 digest.
 *
 * The function decodes `sig_base64` from Base64, then uses mbedTLS to verify
 * the signature against the given PEM public key.
 *
 * @param hash32        Pointer to exactly 32 bytes of binary SHA-256 digest.
 * @param sig_base64    NUL-terminated Base64-encoded signature from the OTA manifest.
 * @param pubkey_pem    NUL-terminated PEM public key (ECDSA / RSA / Ed25519).
 * @param algorithm     NUL-terminated algorithm string: "ECDSA_P256", "RSA_SHA256",
 *                      or "ED25519".
 * @note Emits debug logs for algorithm selection and detailed error logs on verification failure.
 * @return  0  on success (signature valid).
 *         -1  on invalid signature, unsupported algorithm, or internal error.
 */
int fotaVerifySignature(const uint8_t  *hash32,
                        const char     *sig_base64,
                        const char     *pubkey_pem,
                        const char     *algorithm);

/**
 * @brief Base64-decode a NUL-terminated Base64 string into `out`.
 *
 * @param in        NUL-terminated Base64 input.
 * @param out       Output buffer (must be large enough for decoded bytes).
 * @param out_max   Size of `out` in bytes.
 * @param out_len   Set to the number of decoded bytes on success.
 * @return  0 on success, -1 on error.
 */
int fotaBase64Decode(const char *in, uint8_t *out, size_t out_max, size_t *out_len);

#ifdef __cplusplus
}
#endif

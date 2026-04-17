/**
 * @file FotaVerify.cpp
 * @brief BearSSL-based firmware hash and signature verification (ESP8266).
 *
 * Uses BearSSL bundled with the ESP8266 Arduino core (arduino-esp8266 >= 3.1).
 *
 * Supported algorithms:
 *   ECDSA_P256  — ECDSA over NIST P-256, SHA-256
 *   RSA_SHA256  — RSA PKCS#1 v1.5, SHA-256
 *
 * NOT supported on ESP8266:
 *   ED25519     — the ESP8266 BearSSL build does not include Ed25519
 */

#include "FotaVerify.h"
#include "FotaConfig.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

// BearSSL headers (bundled with arduino-esp8266 >= 3.1)
#include <BearSSLHelpers.h>
#include <bearssl/bearssl_ec.h>
#include <bearssl/bearssl_hash.h>
#include <bearssl/bearssl_rsa.h>

// ─── Logging macros ───────────────────────────────────────────────────────────
// Serial-based logging; no ESP-IDF log system on ESP8266.

#if FOTA_LOG_LEVEL >= 4
#define LOGV(fmt, ...) Serial.printf("[V][FotaVerify] " fmt "\r\n", ##__VA_ARGS__)
#else
#define LOGV(fmt, ...) ((void)0)
#endif

#if FOTA_LOG_LEVEL >= 3
#define LOGI(fmt, ...) Serial.printf("[I][FotaVerify] " fmt "\r\n", ##__VA_ARGS__)
#else
#define LOGI(fmt, ...) ((void)0)
#endif

#if FOTA_LOG_LEVEL >= 2
#define LOGW(fmt, ...) Serial.printf("[W][FotaVerify] " fmt "\r\n", ##__VA_ARGS__)
#else
#define LOGW(fmt, ...) ((void)0)
#endif

#if FOTA_LOG_LEVEL >= 1
#define LOGE(fmt, ...) Serial.printf("[E][FotaVerify] " fmt "\r\n", ##__VA_ARGS__)
#else
#define LOGE(fmt, ...) ((void)0)
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Convert lowercase hexadecimal text to raw bytes. */
static int hex_to_bin(const char *hex_str, uint8_t *out, size_t expected_len)
{
	size_t hex_len = strlen(hex_str);
	if (hex_len != expected_len * 2)
	{
		LOGE("hex_to_bin: expected %u hex chars, got %u",
			 (unsigned)(expected_len * 2), (unsigned)hex_len);
		return -1;
	}
	for (size_t i = 0; i < expected_len; i++)
	{
		unsigned int byte_val;
		if (sscanf(hex_str + 2 * i, "%02x", &byte_val) != 1)
		{
			LOGE("hex_to_bin: invalid hex char at pos %u", (unsigned)(2 * i));
			return -1;
		}
		out[i] = (uint8_t)byte_val;
	}
	return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Single character → 6-bit base64 value, -1 for invalid/padding. */
static int8_t s_b64val(uint8_t c)
{
	if (c >= 'A' && c <= 'Z')
		return (int8_t)(c - 'A');
	if (c >= 'a' && c <= 'z')
		return (int8_t)(c - 'a' + 26);
	if (c >= '0' && c <= '9')
		return (int8_t)(c - '0' + 52);
	if (c == '+' || c == '-')
		return 62; /* standard and URL-safe */
	if (c == '/' || c == '_')
		return 63; /* standard and URL-safe */
	return -1;
}

/** @brief Decode a base64-encoded signature or payload buffer. */
int fotaBase64Decode(const char *in, uint8_t *out, size_t out_max, size_t *out_len)
{
	if (!in || !out || !out_len)
	{
		LOGE("fotaBase64Decode: NULL argument");
		return -1;
	}
	size_t inlen = strlen(in);
	size_t outpos = 0;
	uint32_t accum = 0;
	int acbits = 0;
	for (size_t i = 0; i < inlen; i++)
	{
		uint8_t c = (uint8_t)in[i];
		if (c == '=' || c == '\r' || c == '\n' || c == ' ')
			continue;
		int8_t v = s_b64val(c);
		if (v < 0)
		{
			LOGE("fotaBase64Decode: invalid char 0x%02x at pos %u", (unsigned)c, (unsigned)i);
			return -1;
		}
		accum = (accum << 6) | (uint32_t)(uint8_t)v;
		acbits += 6;
		if (acbits >= 8)
		{
			acbits -= 8;
			if (outpos >= out_max)
			{
				LOGE("fotaBase64Decode: output buffer too small (max=%u)", (unsigned)out_max);
				return -1;
			}
			out[outpos++] = (uint8_t)(accum >> acbits);
		}
	}
	*out_len = outpos;
	return 0;
}

// ─────────────────────────────────────────────────────────────────────────────

/** @brief Compute SHA-256 of input bytes and compare with expected hash string. */
int fotaVerifySha256(const uint8_t *data, size_t len, const char *expected_hex)
{
	br_sha256_context ctx;
	br_sha256_init(&ctx);
	br_sha256_update(&ctx, data, len);
	uint8_t computed[32];
	br_sha256_out(&ctx, computed);
	return fotaVerifySha256Digest(computed, expected_hex);
}

/** @brief Compare precomputed SHA-256 digest with expected hash string. */
int fotaVerifySha256Digest(const uint8_t hash32[32], const char *expected_hex)
{
	uint8_t expected[32];
	if (hex_to_bin(expected_hex, expected, 32) != 0)
		return -1;

	// Constant-time comparison to prevent timing side-channel
	volatile uint8_t diff = 0;
	for (int i = 0; i < 32; i++)
		diff |= hash32[i] ^ expected[i];

	if (diff != 0)
	{
		LOGE("SHA-256 mismatch!");
		char computed_hex[65];
		for (int i = 0; i < 32; i++)
			snprintf(computed_hex + 2 * i, 3, "%02x", hash32[i]);
		computed_hex[64] = '\0';
		LOGE("  Expected: %s", expected_hex);
		LOGE("  Computed: %s", computed_hex);
		return -1;
	}

	LOGI("SHA-256 verified OK");
	return 0;
}

// ─────────────────────────────────────────────────────────────────────────────

/** @brief Verify ECDSA or RSA signature over a SHA-256 digest buffer. */
int fotaVerifySignature(const uint8_t *hash32,
						const char *sig_base64,
						const char *pubkey_pem,
						const char *algorithm)
{
	if (!hash32 || !sig_base64 || !pubkey_pem || !algorithm)
	{
		LOGE("fotaVerifySignature: NULL argument");
		return -1;
	}

	// Decode Base64 signature → raw bytes
	uint8_t sig_bytes[512];
	size_t sig_len = 0;
	if (fotaBase64Decode(sig_base64, sig_bytes, sizeof(sig_bytes), &sig_len) != 0)
		return -1;

	LOGV("Signature decoded: %u bytes, algorithm: %s", (unsigned)sig_len, algorithm);

	// ED25519 is not supported on ESP8266
	if (strcmp(algorithm, "ED25519") == 0)
	{
		LOGE("ED25519 is not supported on ESP8266 (BearSSL limitation). "
			 "Configure your FOTA backend to sign with ECDSA_P256.");
		return -1;
	}

	// Parse the PEM public key via the BearSSL Arduino wrapper
	BearSSL::PublicKey pubKey(pubkey_pem);

	if (strcmp(algorithm, "ECDSA_P256") == 0)
	{
		if (!pubKey.isEC())
		{
			LOGE("ECDSA_P256: PEM did not parse as an EC key");
			return -1;
		}
		/* get_default() returns the best available ECDSA ASN.1 verify impl */
		br_ecdsa_vrfy vrfyFn = br_ecdsa_vrfy_asn1_get_default();
		if (!vrfyFn)
		{
			LOGE("ECDSA verify: no implementation available in this BearSSL build");
			return -1;
		}
		uint32_t ok = vrfyFn(br_ec_get_default(),
							 hash32, 32,
							 pubKey.getEC(),
							 sig_bytes, sig_len);
		if (ok)
		{
			LOGI("ECDSA P-256 signature verified OK");
			return 0;
		}
		LOGE("ECDSA P-256 verify failed (bad signature)");
		return -1;
	}

	if (strcmp(algorithm, "RSA_SHA256") == 0)
	{
		if (!pubKey.isRSA())
		{
			LOGE("RSA_SHA256: PEM did not parse as an RSA key");
			return -1;
		}
		/* BR_HASH_OID_SHA256 is already a `const unsigned char*` in BearSSL format */
		uint8_t hash_out[32] = {};
		br_rsa_pkcs1_vrfy vrfyFn = br_rsa_pkcs1_vrfy_get_default();
		if (!vrfyFn)
		{
			LOGE("RSA verify: no implementation available in this BearSSL build");
			return -1;
		}
		uint32_t ok = vrfyFn(
				sig_bytes, sig_len,
				BR_HASH_OID_SHA256, 32,
			pubKey.getRSA(),
			hash_out);
		if (!ok)
		{
			LOGE("RSA-SHA256 verify failed (signature decode error)");
			return -1;
		}
		/* Constant-time comparison of the extracted hash against expected */
		volatile uint8_t diff = 0;
		for (int i = 0; i < 32; i++)
			diff |= hash_out[i] ^ hash32[i];
		if (diff != 0)
		{
			LOGE("RSA-SHA256 hash mismatch");
			return -1;
		}
		LOGI("RSA-SHA256 signature verified OK");
		return 0;
	}

	LOGE("Unknown signature algorithm: %s (supported: ECDSA_P256, RSA_SHA256)", algorithm);
	return -1;
}

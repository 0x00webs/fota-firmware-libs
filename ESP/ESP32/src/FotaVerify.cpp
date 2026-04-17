/**
 * @file FotaVerify.cpp
 * @brief Secure firmware hash + signature verification using mbedTLS.
 *
 * Supported algorithms:
 *  - ECDSA_P256
 *  - RSA_SHA256
 *  - ED25519
 *
 * Compatible with ESP-IDF 5.x (mbedTLS 3.x)
 */

#include "FotaVerify.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "mbedtls/base64.h"
#include "mbedtls/error.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"

static const char *TAG = "FotaVerify";

#define LOGV(fmt, ...) log_v("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGD(fmt, ...) log_d("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGI(fmt, ...) log_i("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGW(fmt, ...) log_w("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGE(fmt, ...) log_e("[%s] " fmt, TAG, ##__VA_ARGS__)

#define SHA256_LEN 32
#define MAX_SIG_LEN 512

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static int hex_char_to_val(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int hex_to_bin(const char *hex, uint8_t *out, size_t expected_len)
{
	size_t hex_len = strlen(hex);

	if (hex_len != expected_len * 2)
	{
		LOGE("hex_to_bin: expected %zu hex chars, got %zu",
			 expected_len * 2, hex_len);
		return -1;
	}

	for (size_t i = 0; i < expected_len; i++)
	{
		int hi = hex_char_to_val(hex[i * 2]);
		int lo = hex_char_to_val(hex[i * 2 + 1]);

		if (hi < 0 || lo < 0)
		{
			LOGE("Invalid hex character at pos %zu", i * 2);
			return -1;
		}

		out[i] = (uint8_t)((hi << 4) | lo);
	}

	return 0;
}

static void bytes_to_hex(const uint8_t *data, size_t len, char *out)
{
	for (size_t i = 0; i < len; i++)
		sprintf(out + (i * 2), "%02x", data[i]);

	out[len * 2] = '\0';
}

static void log_mbedtls_error(const char *msg, int err)
{
	char buf[128];
	mbedtls_strerror(err, buf, sizeof(buf));
	LOGE("%s: %s (%d)", msg, buf, err);
}

/* -------------------------------------------------------------------------- */
/* Base64 decode                                                              */
/* -------------------------------------------------------------------------- */

int fotaBase64Decode(const char *input,
					 uint8_t *out,
					 size_t out_max,
					 size_t *out_len)
{
	int ret = mbedtls_base64_decode(out,
									out_max,
									out_len,
									(const unsigned char *)input,
									strlen(input));

	if (ret != 0)
	{
		log_mbedtls_error("Base64 decode failed", ret);
		return ret;
	}

	return 0;
}

/* -------------------------------------------------------------------------- */
/* SHA256 verification                                                        */
/* -------------------------------------------------------------------------- */

int fotaVerifySha256(const uint8_t *data,
					 size_t len,
					 const char *expected_hex)
{
	uint8_t digest[SHA256_LEN];

	mbedtls_sha256(data, len, digest, 0);

	int ret = fotaVerifySha256Digest(digest, expected_hex);

	mbedtls_platform_zeroize(digest, sizeof(digest));

	return ret;
}

int fotaVerifySha256Digest(const uint8_t hash32[SHA256_LEN],
						   const char *expected_hex)
{
	uint8_t expected[SHA256_LEN];

	if (hex_to_bin(expected_hex, expected, SHA256_LEN) != 0)
		return -1;

	volatile uint8_t diff = 0;

	for (size_t i = 0; i < SHA256_LEN; i++)
		diff |= hash32[i] ^ expected[i];

	if (diff != 0)
	{
		char computed_hex[65];

		bytes_to_hex(hash32, SHA256_LEN, computed_hex);

		LOGE("SHA256 mismatch");
		LOGE("Expected: %s", expected_hex);
		LOGE("Computed: %s", computed_hex);

		mbedtls_platform_zeroize(expected, sizeof(expected));

		return -1;
	}

	LOGI("SHA256 verified OK");

	mbedtls_platform_zeroize(expected, sizeof(expected));

	return 0;
}

/* -------------------------------------------------------------------------- */
/* Signature verification                                                     */
/* -------------------------------------------------------------------------- */

int fotaVerifySignature(const uint8_t hash32[SHA256_LEN],
						const char *sig_base64,
						const char *pubkey_pem,
						const char *algorithm)
{
	if (!hash32 || !sig_base64 || !pubkey_pem || !algorithm)
	{
		LOGE("NULL argument");
		return -1;
	}

	uint8_t sig[MAX_SIG_LEN];
	size_t sig_len = 0;

	int ret = fotaBase64Decode(sig_base64, sig, sizeof(sig), &sig_len);
	if (ret != 0)
		return ret;

	LOGD("Signature decoded (%zu bytes)", sig_len);

	mbedtls_pk_context pk;
	mbedtls_pk_init(&pk);

	ret = mbedtls_pk_parse_public_key(
		&pk,
		(const unsigned char *)pubkey_pem,
		strlen(pubkey_pem) + 1);

	if (ret != 0)
	{
		log_mbedtls_error("Public key parse failed", ret);
		mbedtls_pk_free(&pk);
		return ret;
	}

	/* ------------------------------------------------------------------ */
	/* DOUBLE HASH (compatibility with Node signing)                      */
	/* Node signs SHA256(SHA256(firmware))                                */
	/* ------------------------------------------------------------------ */

	uint8_t verify_hash[SHA256_LEN];
	mbedtls_sha256(hash32, SHA256_LEN, verify_hash, 0);

	char verify_hex_dbg[65];
	bytes_to_hex(verify_hash, SHA256_LEN, verify_hex_dbg);
	LOGD("verify_hash(hex): %s", verify_hex_dbg);

	int result = -1;

	/* ------------------------------------------------------------------ */
	/* ECDSA P256                                                         */
	/* ------------------------------------------------------------------ */

	if (strcmp(algorithm, "ECDSA_P256") == 0)
	{
		if (!mbedtls_pk_can_do(&pk, MBEDTLS_PK_ECKEY))
		{
			LOGE("Key is not EC");
		}
		else
		{
			LOGD("mbedtls_pk_verify params: hash32=%p, sig=%p, sig_len=%zu", hash32, sig, sig_len);
			char hash_hex_dbg[65];
			bytes_to_hex(hash32, SHA256_LEN, hash_hex_dbg);
			LOGD("mbedtls_pk_verify hash32(hex): %s", hash_hex_dbg);
			char sig_hex_dbg[2 * 72 + 1];
			size_t sig_hex_dbg_len = (sig_len > 72 ? 72 : sig_len);
			for (size_t i = 0; i < sig_hex_dbg_len; i++)
				sprintf(sig_hex_dbg + 2 * i, "%02x", sig[i]);
			sig_hex_dbg[2 * sig_hex_dbg_len] = '\0';
			LOGD("mbedtls_pk_verify sig(hex): %s...", sig_hex_dbg);
			LOGD("mbedtls_pk_verify call: mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash32, %d, sig, %zu)", SHA256_LEN, sig_len);
			ret = mbedtls_pk_verify(&pk,
									MBEDTLS_MD_SHA256,
									verify_hash,
									SHA256_LEN,
									sig,
									sig_len);
			LOGD("mbedtls_pk_verify returned: %d", ret);
			if (ret == 0)
			{
				LOGI("ECDSA signature verified");
				result = 0;
			}
			else
			{
				log_mbedtls_error("ECDSA verify failed", ret);
			}
		}
	}

	/* ------------------------------------------------------------------ */
	/* RSA SHA256                                                         */
	/* ------------------------------------------------------------------ */

	else if (strcmp(algorithm, "RSA_SHA256") == 0)
	{
		if (!mbedtls_pk_can_do(&pk, MBEDTLS_PK_RSA))
		{
			LOGE("Key is not RSA");
		}
		else
		{
			LOGD("mbedtls_pk_verify params: hash32=%p, sig=%p, sig_len=%zu", hash32, sig, sig_len);
			char hash_hex_dbg[65];
			bytes_to_hex(hash32, SHA256_LEN, hash_hex_dbg);
			LOGD("mbedtls_pk_verify hash32(hex): %s", hash_hex_dbg);
			char sig_hex_dbg[2 * 72 + 1];
			size_t sig_hex_dbg_len = (sig_len > 72 ? 72 : sig_len);
			for (size_t i = 0; i < sig_hex_dbg_len; i++)
				sprintf(sig_hex_dbg + 2 * i, "%02x", sig[i]);
			sig_hex_dbg[2 * sig_hex_dbg_len] = '\0';
			LOGD("mbedtls_pk_verify sig(hex): %s...", sig_hex_dbg);
			LOGD("mbedtls_pk_verify call: mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash32, %d, sig, %zu)", SHA256_LEN, sig_len);
			ret = mbedtls_pk_verify(&pk,
									MBEDTLS_MD_SHA256,
									verify_hash,
									SHA256_LEN,
									sig,
									sig_len);
			LOGD("mbedtls_pk_verify returned: %d", ret);
			if (ret == 0)
			{
				LOGI("RSA signature verified");
				result = 0;
			}
			else
			{
				log_mbedtls_error("RSA verify failed", ret);
			}
		}
	}

	/* ------------------------------------------------------------------ */
	/* ED25519                                                            */
	/* ------------------------------------------------------------------ */

	else if (strcmp(algorithm, "ED25519") == 0)
	{
#if defined(MBEDTLS_PK_ED25519)

		if (!mbedtls_pk_can_do(&pk, MBEDTLS_PK_ED25519))
		{
			LOGE("Key is not ED25519");
		}
		else
		{
			LOGD("mbedtls_pk_verify params: hash32=%p, sig=%p, sig_len=%zu", hash32, sig, sig_len);
			char hash_hex_dbg[65];
			bytes_to_hex(hash32, SHA256_LEN, hash_hex_dbg);
			LOGD("mbedtls_pk_verify hash32(hex): %s", hash_hex_dbg);
			char sig_hex_dbg[2 * 72 + 1];
			size_t sig_hex_dbg_len = (sig_len > 72 ? 72 : sig_len);
			for (size_t i = 0; i < sig_hex_dbg_len; i++)
				sprintf(sig_hex_dbg + 2 * i, "%02x", sig[i]);
			sig_hex_dbg[2 * sig_hex_dbg_len] = '\0';
			LOGD("mbedtls_pk_verify sig(hex): %s...", sig_hex_dbg);
			LOGD("mbedtls_pk_verify call: mbedtls_pk_verify(&pk, MBEDTLS_MD_NONE, hash32, %d, sig, %zu)", SHA256_LEN, sig_len);
			ret = mbedtls_pk_verify(&pk,
									MBEDTLS_MD_NONE,
									hash32,
									SHA256_LEN,
									sig,
									sig_len);
			LOGD("mbedtls_pk_verify returned: %d", ret);
			if (ret == 0)
			{
				LOGI("ED25519 signature verified");
				result = 0;
			}
			else
			{
				log_mbedtls_error("ED25519 verify failed", ret);
			}
		}

#else
		LOGE("ED25519 not supported by this mbedTLS build");
#endif
	}

	else
	{
		LOGE("Unknown signature algorithm: %s", algorithm);
	}

	mbedtls_pk_free(&pk);
	mbedtls_platform_zeroize(sig, sizeof(sig));

	return result;
}

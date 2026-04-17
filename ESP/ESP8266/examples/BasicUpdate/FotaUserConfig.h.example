/*
 * FotaUserConfig.h.example — copy this to FotaUserConfig.h and fill in your values.
 *
 * This file is automatically included by FotaConfig.h (via __has_include)
 * for EVERY translation unit — both the sketch AND the library (.cpp files).
 * That means settings here genuinely control the library, unlike a #define
 * placed in the .ino file which only affects the sketch translation unit.
 *
 * No changes to platformio.ini build_flags are needed.
 *
 * ⚠  Keep FotaUserConfig.h out of version control if it contains real
 *    credentials.  Commit this .example file instead.
 */

#pragma once

// ── Server ───────────────────────────────────────────────────────────────────
#define FOTA_SERVER_URL "https://your-fota-server.example.com"

// ── Device identity ───────────────────────────────────────────────────────────
#define FOTA_HARDWARE_MODEL  "ESP8266-M1"
#define FOTA_CURRENT_VERSION "1.0.0"   // override at build time if needed
#define FOTA_DEVICE_ID       "esp8266-001"

// ── Authentication ────────────────────────────────────────────────────────────
#define FOTA_AUTH_TOKEN "fota_d_your_device_token_here"

// ── Signature verification ────────────────────────────────────────────────────
// 0 = disabled (development / no signing server)
// 1 = enabled  (recommended for production)
// NOTE: Ed25519 is NOT supported on ESP8266 — use ECDSA_P256 or RSA_SHA256.
#define FOTA_VERIFY_SIGNATURE 0

// ── TLS – root CA certificate (optional) ─────────────────────────────────────
// Uncomment and paste your DER-encoded PEM to enable full TLS peer
// verification.  Leave commented out ONLY for local dev (insecure in prod).
//
// #define FOTA_SERVER_CA_CERT \
//   "-----BEGIN CERTIFICATE-----\n" \
//   "...\n" \
//   "-----END CERTIFICATE-----\n"

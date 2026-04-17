#pragma once

/*
 * FotaConfig.h — Compile-time configuration for the NodeWave FOTA ESP8266 client
 *
 * v1.0.0
 *
 * HOW TO USE
 * ----------
 * The recommended and cleanest approach is to create a file called
 * FotaUserConfig.h alongside your sketch (.ino) with all your overrides:
 *
 *   // FotaUserConfig.h
 *   #define FOTA_SERVER_URL       "https://fota.mycompany.com"
 *   #define FOTA_HARDWARE_MODEL   "ESP8266"
 *   #define FOTA_CURRENT_VERSION  "1.0.0"
 *   #define FOTA_AUTH_TOKEN       "fota_d_..."
 *   #define FOTA_DEVICE_ID        "sensor-01"
 *   #define FOTA_VERIFY_SIGNATURE 0
 *
 * FotaConfig.h automatically includes FotaUserConfig.h if it exists in the
 * compiler include path (PlatformIO adds your src_dir automatically, so no
 * build_flags changes are needed).  Both the sketch and the library will see
 * the same settings.
 *
 * Alternatively you can still #define macros before #include <FotaClient.h>
 * in your .ino file, but that only affects the sketch translation unit —
 * NOT the library.  Use FotaUserConfig.h to configure the library.
 *
 * NOTE: Ed25519 signature verification is NOT supported on ESP8266.
 *       Use ECDSA_P256 (recommended) or RSA_SHA256.
 */

// Auto-include the per-project user config if it exists anywhere on the
// compiler include path (typically next to your .ino / main.cpp).
#if __has_include("FotaUserConfig.h")
#  include "FotaUserConfig.h"
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 1  SERVER
// ════════════════════════════════════════════════════════════════════════════

#ifndef FOTA_SERVER_URL
/** Base URL of the FOTA platform backend — no trailing slash. */
#define FOTA_SERVER_URL "https://api.example.com"
#endif

#ifndef FOTA_API_PREFIX
/** API path prefix — must match the NestJS globalPrefix (default: /api/v1). */
#define FOTA_API_PREFIX "/api/v1"
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 2  DEVICE IDENTITY
// ════════════════════════════════════════════════════════════════════════════

#ifndef FOTA_HARDWARE_MODEL
/** Hardware model — must match firmwares.hardware_model in the database. */
#define FOTA_HARDWARE_MODEL "ESP8266"
#endif

#ifndef FOTA_CURRENT_VERSION
/** Semver of the firmware currently running on the device (no leading "v"). */
#define FOTA_CURRENT_VERSION "0.0.0"
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 3  AUTHENTICATION
// ════════════════════════════════════════════════════════════════════════════

#ifndef FOTA_AUTH_TOKEN
/**
 * Operator or Admin Bearer JWT for all FOTA API requests.
 *
 * Obtain from: FOTA Dashboard → Profile → API Keys
 *   (or use a Supabase long-lived service key).
 *
 * Leave as "" and call FotaClient::setAuthToken() at runtime to avoid
 * embedding credentials at compile time (recommended for production).
 */
#define FOTA_AUTH_TOKEN ""
#endif

#ifndef FOTA_DEVICE_ID
/**
 * Device business-key for campaign-aware OTA resolution.
 *
 * Must match the 'device_id' field in the FOTA platform devices table.
 * When set, GET /ota/check includes ?device_id= so the server returns
 * campaign-targeted firmware ahead of the global latest release.
 *
 * Leave as "" and call FotaClient::setDeviceId() at runtime if the ID
 * is derived from the MAC address, or a provisioning step.
 */
#define FOTA_DEVICE_ID ""
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 4  TLS / SERVER CERTIFICATE
// ════════════════════════════════════════════════════════════════════════════

#ifndef FOTA_SERVER_CA_CERT
/**
 * PEM root CA certificate for verifying the FOTA server's TLS certificate.
 *
 * Uses BearSSL — the CA cert is loaded via BearSSL::X509List internally.
 *
 * Obtain with:
 *   openssl s_client -connect api.example.com:443 -showcerts 2>/dev/null \
 *     | openssl x509 -outform PEM
 *
 * Embed as a multi-line string:
 *   #define FOTA_SERVER_CA_CERT         \
 *     "-----BEGIN CERTIFICATE-----\n"   \
 *     "MIIBxTCCAW+gAwIBAgIJAP...\n"    \
 *     "-----END CERTIFICATE-----\n"
 *
 * Set to nullptr to skip TLS peer verification.
 * WARNING: nullptr is NEVER safe in production — MITM attacks will succeed.
 */
#define FOTA_SERVER_CA_CERT nullptr
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 5  FIRMWARE VERIFICATION
// ════════════════════════════════════════════════════════════════════════════

#ifndef FOTA_VERIFY_SHA256
/**
 * SHA-256 integrity check (1 = enabled, 0 = disabled). Default: 1.
 * Disable only during development / debugging.
 */
#define FOTA_VERIFY_SHA256 1
#endif

#ifndef FOTA_VERIFY_SIGNATURE
/**
 * Digital signature verification (1 = enabled, 0 = disabled). Default: 1.
 *
 * Supported algorithms on ESP8266 (auto-detected from OTA manifest):
 *   "ECDSA_P256"  — ECDSA over NIST P-256, SHA-256  (recommended)
 *   "RSA_SHA256"  — RSA PKCS#1 v1.5, SHA-256
 *
 * NOTE: "ED25519" is NOT supported on ESP8266 — the SDK's mbedTLS build
 *       does not include the Ed25519 key type. Configure your backend to
 *       sign with ECDSA_P256.
 */
#define FOTA_VERIFY_SIGNATURE 1
#endif

#ifndef FOTA_SIGNING_PUBLIC_KEY
/**
 * PEM public key for firmware signature verification.
 *
 * How to obtain:
 *   curl -H "Authorization: Bearer <token>" \
 *        https://api.example.com/api/v1/firmware/public-key | jq -r .public_key_pem
 *
 * Embed at compile time (most secure — key cannot be swapped at runtime):
 *   #define FOTA_SIGNING_PUBLIC_KEY           \
 *     "-----BEGIN PUBLIC KEY-----\n"           \
 *     "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcD...\n" \
 *     "-----END PUBLIC KEY-----\n"
 *
 * Or leave as empty string ("") and:
 *   - Enable FOTA_AUTO_FETCH_PUBLIC_KEY to retrieve it automatically, OR
 *   - Call FotaClient::fetchPublicKey() manually before performUpdate().
 */
#define FOTA_SIGNING_PUBLIC_KEY ""
#endif

#ifndef FOTA_AUTO_FETCH_PUBLIC_KEY
/**
 * When 1 and FOTA_SIGNING_PUBLIC_KEY is empty, the client automatically
 * fetches the signing public key from GET /firmware/public-key the first
 * time an update manifest is received.
 *
 * Default: 1.
 */
#define FOTA_AUTO_FETCH_PUBLIC_KEY 1
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 6  PROGRESS REPORTING
// ════════════════════════════════════════════════════════════════════════════

#ifndef FOTA_REPORT_PROGRESS
/**
 * Report OTA lifecycle stages to the platform via POST /ota/device/progress.
 * Stages reported: DOWNLOADING → VERIFYING → INSTALLING → COMPLETED / FAILED.
 *
 * Requires FOTA_DEVICE_ID; silently noops if it is empty.
 * Progress failures are non-fatal and never abort the update.
 * 1 = enabled (default), 0 = disabled.
 */
#define FOTA_REPORT_PROGRESS 1
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 7  NETWORKING
// ════════════════════════════════════════════════════════════════════════════

#ifndef FOTA_HTTP_TIMEOUT_MS
/** HTTP request/receive timeout in milliseconds. Default: 30 s. */
#define FOTA_HTTP_TIMEOUT_MS 30000
#endif

#ifndef FOTA_MAX_FIRMWARE_SIZE
/**
 * Maximum accepted firmware binary size in bytes.
 * ESP8266 typically uses 1 MiB OTA slots. Default: 1 MiB.
 */
#define FOTA_MAX_FIRMWARE_SIZE (1024 * 1024)
#endif

#ifndef FOTA_URL_EXPIRY_MARGIN_S
/**
 * Seconds of margin before treating a signed download URL as expired.
 * (manifest.expires_in − FOTA_URL_EXPIRY_MARGIN_S = effective URL lifetime)
 * Default: 30 s.
 */
#define FOTA_URL_EXPIRY_MARGIN_S 30
#endif

#ifndef FOTA_RETRY_COUNT
/**
 * Number of retries on transient failures.
 * Security failures (SHA-256 mismatch, invalid signature) are never retried.
 * Default: 3.
 */
#define FOTA_RETRY_COUNT 3
#endif

#ifndef FOTA_RETRY_DELAY_MS
/** Milliseconds to wait between retry attempts. Default: 5000 ms. */
#define FOTA_RETRY_DELAY_MS 5000
#endif

#ifndef FOTA_CONNECT_TIMEOUT_MS
/**
 * TLS connect + BearSSL handshake timeout in milliseconds.
 * BearSSL handshakes on ESP8266 can take 1–3 s. Default: 15 s.
 */
#define FOTA_CONNECT_TIMEOUT_MS 15000
#endif

#ifndef FOTA_DOWNLOAD_TIMEOUT_MS
/**
 * Maximum time allowed to stream the complete firmware binary.
 * Default: 90 s.
 */
#define FOTA_DOWNLOAD_TIMEOUT_MS 90000 // 90s, but clamp to 65535u for uint16_t usage
#endif

#ifndef FOTA_USER_AGENT
/**
 * HTTP User-Agent header sent with every FOTA API and download request.
 * Default: "FotaClient-ESP8266/1.0.0 (arduino-esp8266)"
 */
#define FOTA_USER_AGENT "FotaClient-ESP8266/1.0.0 (arduino-esp8266)"
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 8  BEHAVIOUR
// ════════════════════════════════════════════════════════════════════════════

#ifndef FOTA_REBOOT_ON_SUCCESS
/**
 * When 1, performUpdate() calls ESP.restart() automatically after a
 * successful flash.  When 0 (default), FotaResult::OK signals the caller
 * to reboot at a convenient time.
 * Default: 0.
 */
#define FOTA_REBOOT_ON_SUCCESS 0
#endif

#ifndef FOTA_WATCHDOG_FEED
/**
 * When 1, the firmware download loop calls yield() after each read chunk,
 * preventing software WDT resets on slow connections.
 * Default: 1.
 */
#define FOTA_WATCHDOG_FEED 1
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 9  LOGGING
// ════════════════════════════════════════════════════════════════════════════

#ifndef FOTA_LOG_LEVEL
/**
 * Controls verbosity of Serial output from FotaClient and FotaVerify.
 *
 *   0 — silent
 *   1 — errors only
 *   2 — errors + warnings
 *   3 — errors + warnings + info   (default)
 *   4 — full debug/verbose
 */
#define FOTA_LOG_LEVEL 3
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 10  WIFI RECONNECT (retry loop)
// ════════════════════════════════════════════════════════════════════════════

#ifndef FOTA_WIFI_RECONNECT_TIMEOUT_MS
/**
 * Maximum time (ms) to wait for WiFi to reconnect before a retry attempt.
 * Set to 0 to disable the reconnect wait.
 * Default: 30000 ms (30 s).
 */
#define FOTA_WIFI_RECONNECT_TIMEOUT_MS 30000
#endif

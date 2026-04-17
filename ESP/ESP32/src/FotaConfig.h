#pragma once

/*
 * FotaConfig.h — Compile-time configuration for the NodeWave FOTA ESP32 client
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
 *   #define FOTA_HARDWARE_MODEL   "ESP32-WROOM-32"
 *   #define FOTA_CURRENT_VERSION  "1.0.0"
 *   #define FOTA_AUTH_TOKEN       "fota_d_..."
 *   #define FOTA_DEVICE_ID        "sensor-cabinet-01"
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
#define FOTA_HARDWARE_MODEL "ESP32-WROOM-32"
#endif

#ifndef FOTA_CURRENT_VERSION
/** Semver of the firmware currently running on the device (no leading "v"). */
#define FOTA_CURRENT_VERSION "1.0.0"
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
 * is derived from the MAC address, NVS, or a provisioning step.
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
 * Supported algorithms (auto-detected from OTA manifest):
 *   "ECDSA_P256"  — ECDSA over NIST P-256, SHA-256  (recommended)
 *   "RSA_SHA256"  — RSA PKCS#1 v1.5, SHA-256
 *   "ED25519"     — Ed25519  (requires arduino-esp32 >= 2.0 / ESP-IDF >= 5.0)
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
 * fetches the signing public key from GET /api/v1/firmware/public-key the first
 * time an update manifest is received.
 *
 * Requires FOTA_SERVER_CA_CERT for authenticated key retrieval (insecure
 * fetch without a CA cert is blocked). Default: 1.
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
 * Payloads larger than this are rejected before allocation to prevent OOM.
 * Default: 4 MiB.
 */
#define FOTA_MAX_FIRMWARE_SIZE (4 * 1024 * 1024)
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
 * Number of retries on transient failures (network errors, timeouts,
 * non-200 HTTP responses, JSON parse errors).
 *
 * Security failures (SHA-256 mismatch, invalid signature) and configuration
 * errors (missing token, missing key) are never retried.
 * Default: 3.
 */
#define FOTA_RETRY_COUNT 3
#endif

#ifndef FOTA_RETRY_DELAY_MS
/**
 * Milliseconds to wait between retry attempts. Default: 5000 ms (5 s).
 */
#define FOTA_RETRY_DELAY_MS 5000
#endif

#ifndef FOTA_CONNECT_TIMEOUT_MS
/**
 * TLS connect + handshake timeout in milliseconds.
 *
 * Applies to the initial TCP connection and TLS negotiation phase,
 * separately from the data transfer. On congested IoT networks the
 * handshake can stall; tune this independently of FOTA_HTTP_TIMEOUT_MS.
 * Default: 10 s.
 */
#define FOTA_CONNECT_TIMEOUT_MS 10000
#endif

#ifndef FOTA_DOWNLOAD_TIMEOUT_MS
/**
 * Maximum time allowed to stream the complete firmware binary.
 *
 * Firmware images can be several MiB. On cellular or lossy LPWAN links
 * the throughput may be low. Set this considerably higher than
 * FOTA_HTTP_TIMEOUT_MS, which applies to short JSON API calls only.
 * Default: 90 s.
 */
#define FOTA_DOWNLOAD_TIMEOUT_MS 90000
#endif

#ifndef FOTA_USER_AGENT
/**
 * HTTP User-Agent header sent with every FOTA API and download request.
 *
 * The server can use this to filter requests by client version or to
 * enforce a minimum library version before serving firmware.
 * Default: "FotaClient-ESP32/1.0.0 (arduino-esp32)"
 */
#define FOTA_USER_AGENT "FotaClient-ESP32/1.0.0 (arduino-esp32)"
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 8  BEHAVIOUR
// ════════════════════════════════════════════════════════════════════════════

#ifndef FOTA_REBOOT_ON_SUCCESS
/**
 * When 1, performUpdate() calls esp_restart() automatically after a
 * successful flash, so the sketch never needs to call ESP.restart().
 * When 0 (default), the return value FotaResult::OK signals the caller
 * to reboot at a convenient time.
 * Default: 0.
 */
#define FOTA_REBOOT_ON_SUCCESS 0
#endif

#ifndef FOTA_ROLLBACK_ENABLED
/**
 * When 1, FotaClient::begin() calls esp_ota_mark_app_valid_cancel_rollback()
 * to confirm the running OTA partition immediately on startup.
 *
 * The ESP32 bootloader places a freshly-flashed partition in the
 * ESP_OTA_IMG_PENDING_VERIFY state. If the app never confirms itself
 * and the device reboots (e.g. WDT reset), the bootloader rolls back to
 * the previous firmware — which is the correct safety behaviour.
 *
 * By calling begin() early in setup() you confirm "this firmware booted
 * cleanly" and cancel the pending rollback. DO NOT set this to 0 unless
 * your application manages OTA confirmation explicitly via the esp-idf
 * esp_ota API.
 * Default: 1.
 */
#define FOTA_ROLLBACK_ENABLED 1
#endif

#ifndef FOTA_WATCHDOG_FEED
/**
 * When 1, the firmware download loop calls yield() after each read chunk.
 *
 * This yields control to lower-priority tasks (including the IDLE task
 * that feeds the esp-idf task watchdog timer), preventing WDT resets
 * on slow or congested connections during long downloads.
 *
 * Disable only if your sketch feeds the WDT explicitly using the
 * esp_task_wdt API, or if yield() latency is unacceptable inside an
 * RTOS-pinned real-time task.
 * Default: 1.
 */
#define FOTA_WATCHDOG_FEED 1
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 9  LOGGING
// ════════════════════════════════════════════════════════════════════════════

#ifndef FOTA_LOG_LEVEL
/**
 * Controls the verbosity of ESP_LOG output from FotaClient and FotaVerify.
 *
 *   0 — silent  (no output from this library)
 *   1 — errors only
 *   2 — errors + warnings
 *   3 — errors + warnings + info   (default)
 *   4 — full debug (verbose)
 *
 * Applied via esp_log_level_set() inside FotaClient::begin().
 * Does not affect log levels of other components.
 */
#define FOTA_LOG_LEVEL 3
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 11  POST-BOOT HEALTH WATCHDOG (optional)
// ════════════════════════════════════════════════════════════════════════════

#ifndef FOTA_HEALTH_TIMEOUT_MS
/**
 * Time window (ms) in which the sketch must call FotaClient::markHealthy()
 * after begin() on a freshly-flashed OTA partition.
 *
 * Requires FOTA_ROLLBACK_ENABLED == 1.
 *
 * When > 0:
 *   - begin() detects a PENDING_VERIFY partition and arms a deadline instead
 *     of immediately calling esp_ota_mark_app_valid_cancel_rollback().
 *   - The sketch calls markHealthy() once it has verified that the firmware
 *     is functioning (WiFi connected, sensors OK, server reachable, etc.).
 *   - tick() must be called from loop(). If the deadline passes without a
 *     markHealthy() call, tick() invokes
 *     esp_ota_mark_app_invalid_rollback_and_reboot() and the device reboots
 *     back into the previous (known-good) firmware automatically.
 *
 * When 0 (default): begin() validates the partition immediately, matching
 * the v1.0.0 behaviour (no deferred check).
 *
 * Recommended value: 300000 (5 minutes) — enough to complete a WiFi
 * association, DHCP, and one round-trip to the application server.
 */
#define FOTA_HEALTH_TIMEOUT_MS 0
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 12  WIFI RECONNECT (retry loop)
// ════════════════════════════════════════════════════════════════════════════

#ifndef FOTA_WIFI_RECONNECT_TIMEOUT_MS
/**
 * Maximum time (ms) to wait for WiFi to reconnect before a retry attempt.
 *
 * When WiFi drops mid-download and a retry is triggered, performUpdate()
 * waits up to this long for WiFi.status() == WL_CONNECTED before sleeping
 * the normal retry delay. This avoids immediately burning a retry on a
 * link that is guaranteed to fail.
 *
 * Set to 0 to disable the reconnect wait (retry immediately, existing v1.0.0
 * behaviour).
 * Default: 30000 ms (30 s).
 */
#define FOTA_WIFI_RECONNECT_TIMEOUT_MS 30000
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 10  SD CARD TEMP STORAGE (optional)
// ════════════════════════════════════════════════════════════════════════════

#ifndef FOTA_USE_SD_TEMP
/**
 * Download firmware to an SD/SDMMC card as a temporary file before flashing,
 * instead of streaming directly to the OTA partition.
 *
 * When enabled the update flow becomes two-pass:
 *   Pass 1 — HTTP → SD file  (SHA-256 computed incrementally during download)
 *   Pass 2 — Verify SHA-256 + signature (BEFORE touching the OTA partition)
 *          → SD file → esp_ota_write() → commit boot partition
 *   Cleanup — temp file deleted on both success AND failure paths.
 *
 * Benefits over the default single-pass stream:
 *   - SHA-256 / signature are fully checked BEFORE the OTA partition is
 *     written, so a hash mismatch never leaves a partially-written partition.
 *   - The download can be retried independently without issuing esp_ota_begin
 *     again (reduces SPI flash write cycles on transient network failures).
 *   - Useful on boards without PSRAM where heap is tight: the 4 KB chunk
 *     buffer is the only RAM cost; the full binary lives on the SD card.
 *
 * Requirements:
 *   - Initialize SD (SD.begin()) or SD_MMC (SD_MMC.begin()) BEFORE calling
 *     performUpdate(). FotaClient does NOT call begin() on the SD library.
 *   - Sufficient free space on the card (at least manifest.file_size bytes).
 *   - The temp file is always deleted after the update (success or failure).
 *
 * Default: 0 (stream directly to OTA partition — no SD card required).
 */
#define FOTA_USE_SD_TEMP 0
#endif

#ifndef FOTA_SD_TEMP_PATH
/**
 * Absolute path of the temporary firmware file created on the SD card.
 *
 * Must start with '/' (root of the SD filesystem). The file is created,
 * written, verified, flashed from, and then automatically deleted.
 * Any pre-existing file at this path is overwritten.
 *
 * Example override:
 *   #define FOTA_SD_TEMP_PATH "/ota/firmware_update.bin"
 *
 * Default: "/fota_tmp.bin"
 */
#define FOTA_SD_TEMP_PATH "/fota_tmp.bin"
#endif

#ifndef FOTA_SD_FS
/**
 * SD filesystem instance to use for the temporary file.
 *
 * Default: SD  (uses the SPI SD library — #include <SD.h> in your sketch).
 * For SDMMC:  #define FOTA_SD_FS SD_MMC  (and #include <SD_MMC.h>)
 *
 * Any object that exposes .open(), .remove(), and .exists() with the
 * Arduino SD API is accepted.
 */
#define FOTA_SD_FS SD
#endif

#ifndef FOTA_RESUME_SD_DOWNLOAD
/**
 * When 1 (default) and FOTA_USE_SD_TEMP == 1, a failed download is resumed
 * from where it left off instead of restarting from byte 0.
 *
 * On resume:
 *   1. The existing partial file is re-read and fed into the SHA-256 context.
 *   2. An HTTP Range: bytes=<offset>- header is added to the request.
 *   3. If the server returns 206, the download appends to the partial file.
 *   4. If the server returns 200 (no Range support), the download restarts.
 *
 * On lossy / slow connections (cellular, LPWAN) this avoids re-downloading
 * the same bytes on every transient failure.
 * Has no effect when FOTA_USE_SD_TEMP == 0 (streaming path cannot resume).
 * Default: 1.
 */
#define FOTA_RESUME_SD_DOWNLOAD 1
#endif

// ════════════════════════════════════════════════════════════════════════════
// § FSOTA  FILESYSTEM OTA CONFIGURATION
// ════════════════════════════════════════════════════════════════════════════
// FsotaClient uses these defaults. Override them in FotaUserConfig.h (before
// #include "FotaClient.h") using the same #ifndef guard pattern.
// ════════════════════════════════════════════════════════════════════════════

#ifndef FSOTA_SERVER_URL
/** FOTA server base URL — inherits from firmware FOTA by default. */
#define FSOTA_SERVER_URL FOTA_SERVER_URL
#endif

#ifndef FSOTA_CURRENT_FS_VERSION
/**
 * Semantic version of the filesystem image currently flashed on the device.
 * Must be a "major.minor.patch" string, e.g. "1.2.3".
 * Default: "0.0.0" — will always trigger an update if one exists.
 */
#define FSOTA_CURRENT_FS_VERSION "0.0.0"
#endif

#ifndef FSOTA_FS_TYPE
/**
 * Type of filesystem in use. Sent to the server during the version check so
 * it can return images of the correct type.
 * Valid values: "LITTLEFS", "SPIFFS", "FATFS", "CUSTOM"
 * Default: "LITTLEFS"
 */
#define FSOTA_FS_TYPE "LITTLEFS"
#endif

#ifndef FSOTA_PARTITION_LABEL
/**
 * ESP-IDF partition table label for the filesystem partition.
 * FsotaClient calls esp_partition_find_first() with this label.
 * If not found, it falls back to the first DATA/SPIFFS-subtype partition.
 * Default: "spiffs" (matches the default Arduino ESP32 partition tables).
 */
#define FSOTA_PARTITION_LABEL "spiffs"
#endif

#ifndef FSOTA_VERIFY_SHA256
/**
 * When 1, FsotaClient computes the SHA-256 of the downloaded image while
 * streaming and compares it with the checksum from the server.
 * On mismatch the partition is erased and ERR_SHA256 is returned.
 * Default: 1.
 */
#define FSOTA_VERIFY_SHA256 1
#endif

#ifndef FSOTA_DOWNLOAD_TIMEOUT_MS
/**
 * Maximum time (ms) for the filesystem image download stream.
 * Filesystem images tend to be smaller than firmware, so you can afford
 * a shorter timeout, but increase on slow links.
 * Default: inherits FOTA_DOWNLOAD_TIMEOUT_MS.
 */
#define FSOTA_DOWNLOAD_TIMEOUT_MS FOTA_DOWNLOAD_TIMEOUT_MS
#endif

#ifndef FSOTA_REBOOT_ON_SUCCESS
/**
 * When 1, FsotaClient::performUpdate() calls esp_restart() automatically
 * after successfully flashing the filesystem image.
 * When 0 (default) return value FsotaResult::OK signals the caller to
 * reboot at a convenient time (e.g. after saving state or closing files).
 * Default: 0.
 */
#define FSOTA_REBOOT_ON_SUCCESS 0
#endif

#ifndef FSOTA_USER_AGENT
/**
 * HTTP User-Agent string for all FsotaClient requests.
 */
#define FSOTA_USER_AGENT "FsotaClient-ESP32/1.0.0 (arduino-esp32)"
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 13  PER-CHIP NOTES (reference only)
// ════════════════════════════════════════════════════════════════════════════
//
// This library is chip-agnostic: all code uses portable Arduino + ESP-IDF
// APIs (esp_ota_*, esp_https_ota, mbedTLS, WiFiClientSecure).
//
// ┌──────────────┬──────────┬────────┬────────────────────────────────────┐
// │ Chip         │ Core     │ RAM    │ Notes                              │
// ├──────────────┼──────────┼────────┼────────────────────────────────────┤
// │ ESP32        │ LX6 dual │ 520 KB │ Needs 32 KB loopTask stack (see    │
// │              │          │        │ getArduinoLoopTaskStackSize below)  │
// │ ESP32-S2     │ LX7 mono │ 320 KB │ Single-core: OTA blocks all tasks  │
// │ ESP32-S3     │ LX7 dual │ 512 KB │ Recommended target, USB-CDC        │
// │ ESP32-C3     │ RV32 1c  │ 400 KB │ Use upload_protocol = esptool      │
// │ ESP32-C6     │ RV32 1c  │ 512 KB │ Wi-Fi 6, use upload_protocol=esptool│
// │ ESP32-H2     │ RV32 1c  │ 320 KB │ No Wi-Fi — needs IP gateway for OTA│
// │ ESP32-C2     │ RV32 1c  │ 272 KB │ Tight heap; set FOTA_MAX_FIRMWARE_ │
// │              │          │        │ SIZE ≤ 2 MiB; consider SD temp     │
// └──────────────┴──────────┴────────┴────────────────────────────────────┘
//
// REQUIRED in every sketch (all chips) — mbedTLS needs ~20 KB stack:
//
//   size_t getArduinoLoopTaskStackSize() { return 32 * 1024; }
//
// Without this override the default 8 KB Arduino loop task stack causes
// stack overflow during the TLS handshake on all ESP32 variants.
//
// For ESP32-C2 / tight-RAM boards, also add:
//   #define FOTA_MAX_FIRMWARE_SIZE (2 * 1024 * 1024)  // 2 MiB
//   #define FOTA_USE_SD_TEMP 1                         // if SD available

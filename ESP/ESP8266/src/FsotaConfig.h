#pragma once

/*
 * FsotaConfig.h — Compile-time configuration for the NodeWave FOTA ESP8266 filesystem OTA client
 *
 * v1.0.0
 *
 * HOW TO USE
 * ----------
 * Override any of these defaults in FotaUserConfig.h (placed alongside your sketch):
 *
 *   // FotaUserConfig.h
 *   #define FSOTA_CURRENT_FS_VERSION  "1.0.0"
 *   #define FSOTA_FS_TYPE             "LITTLEFS"
 *   #define FSOTA_VERIFY_SHA256       1
 *
 * FsotaConfig.h is included AFTER FotaConfig.h so all FOTA_* symbols are already
 * defined and can be used as fallbacks here (e.g. FSOTA_SERVER_URL inherits
 * FOTA_SERVER_URL automatically).
 *
 * NOTE: FSOTA_SERVER_URL, FOTA_AUTH_TOKEN, FOTA_HARDWARE_MODEL, FOTA_API_PREFIX,
 * FOTA_CONNECT_TIMEOUT_MS and FOTA_HTTP_TIMEOUT_MS are shared with FotaConfig.h.
 * You do NOT need to redefine them here if you have already set them for the firmware
 * FOTA client.
 */

// Auto-include user config if available (PlatformIO adds src_dir to include path)
#if __has_include("FotaUserConfig.h")
#  include "FotaUserConfig.h"
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 1  SERVER  (inherit from FotaConfig.h by default)
// ════════════════════════════════════════════════════════════════════════════

#ifndef FSOTA_SERVER_URL
/** FOTA platform base URL. Inherits from FOTA_SERVER_URL by default. */
#define FSOTA_SERVER_URL FOTA_SERVER_URL
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 2  DEVICE IDENTITY
// ════════════════════════════════════════════════════════════════════════════

#ifndef FSOTA_CURRENT_FS_VERSION
/**
 * Semantic version of the filesystem image currently on the device.
 * Must be "major.minor.patch", e.g. "1.2.3".
 * Default: "0.0.0" — always triggers an update if one exists on the server.
 */
#define FSOTA_CURRENT_FS_VERSION "0.0.0"
#endif

#ifndef FSOTA_FS_TYPE
/**
 * Type of filesystem in use. Sent to the server during the version check.
 * Valid values: "LITTLEFS", "SPIFFS", "FATFS", "CUSTOM"
 * Default: "LITTLEFS"
 *
 * Note: On ESP8266, both LittleFS and SPIFFS use the Arduino Updater U_FS
 * flag — the filesystem type distinction is only meaningful to the server.
 */
#define FSOTA_FS_TYPE "LITTLEFS"
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 3  VERIFICATION
// ════════════════════════════════════════════════════════════════════════════

#ifndef FSOTA_VERIFY_SHA256
/**
 * When 1, FsotaClient computes SHA-256 of the downloaded image (using BearSSL
 * br_sha256) and compares it with the server-provided checksum before calling
 * Update.end(). The Updater is aborted on mismatch.
 * Default: 1.
 */
#define FSOTA_VERIFY_SHA256 1
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 4  TIMEOUTS
// ════════════════════════════════════════════════════════════════════════════

#ifndef FSOTA_DOWNLOAD_TIMEOUT_MS
/**
 * Maximum time (ms) for the filesystem image download stream.
 * Filesystem images are typically smaller than firmware; the inherited
 * FOTA_DOWNLOAD_TIMEOUT_MS (60 s default on ESP8266) is usually sufficient.
 * Default: inherits FOTA_DOWNLOAD_TIMEOUT_MS.
 */
#define FSOTA_DOWNLOAD_TIMEOUT_MS FOTA_DOWNLOAD_TIMEOUT_MS
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 5  BEHAVIOUR
// ════════════════════════════════════════════════════════════════════════════

#ifndef FSOTA_REBOOT_ON_SUCCESS
/**
 * When 1, FsotaClient::performUpdate() calls ESP.restart() automatically
 * after successfully flashing the filesystem image.
 * When 0 (default), FsotaResult::OK signals the caller to reboot at a
 * convenient time (e.g. after unmounting the filesystem or saving state).
 * Default: 0.
 */
#define FSOTA_REBOOT_ON_SUCCESS 0
#endif

// ════════════════════════════════════════════════════════════════════════════
// § 6  HTTP IDENTITY
// ════════════════════════════════════════════════════════════════════════════

#ifndef FSOTA_USER_AGENT
/**
 * HTTP User-Agent sent in every FsotaClient request.
 */
#define FSOTA_USER_AGENT "FsotaClient-ESP8266/1.0.0 (arduino-esp8266)"
#endif

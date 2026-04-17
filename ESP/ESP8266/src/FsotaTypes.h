#pragma once
#include <Arduino.h>

/**
 * @file FsotaTypes.h
 * @brief Enums, structs, callbacks, and helpers for the FOTA ESP8266 filesystem OTA client.
 *
 * Mirrors FotaTypes.h but covers filesystem-image (LittleFS / SPIFFS / FATFS)
 * updates instead of firmware binary updates.
 */

// ─── Result codes ─────────────────────────────────────────────────────────────

enum class FsotaResult : int8_t
{
	OK = 0,				///< Filesystem image flashed successfully
	NO_UPDATE = 1,		///< Server reports device is already on the latest image
	ERR_WIFI = -1,		///< WiFi not connected
	ERR_HTTP = -2,		///< HTTP request failed (non-200/302, timeout)
	ERR_JSON = -3,		///< Failed to parse server JSON response
	ERR_SHA256 = -4,	///< SHA-256 digest mismatch — partition erased for safety
	ERR_FLASH = -5,		///< Updater write or commit failed
	ERR_SIZE = -6,		///< Image exceeds partition capacity
	ERR_TOKEN = -7,		///< Auth token not configured
	ERR_DOWNLOAD = -8,	///< Download stream read error or timeout
	ERR_ALLOC = -9,		///< Heap allocation failure
};

// ─── Filesystem manifest (decoded from /api/v1/filesystem/check) ──────────────

/**
 * FsotaManifest holds data returned by GET /api/v1/filesystem/check.
 */
struct FsotaManifest
{
	bool update_available;		///< True when a newer filesystem image exists
	char image_id[37];			///< UUIDv4 of the filesystem_images record
	char version[32];			///< Image version string (e.g. "1.2.3")
	char hardware_model[64];	///< Hardware model string
	char fs_type[16];			///< "LITTLEFS", "SPIFFS", "FATFS", or "CUSTOM"
	char hash[65];				///< SHA-256 hex digest (64 hex chars + NUL)
	char hash_algorithm[16];	///< Always "sha256"
	uint32_t file_size;			///< Expected binary size in bytes (0 = unknown)
	char download_url[1024];	///< Short-lived signed URL for the image binary
	uint32_t expires_in;		///< URL validity in seconds
	char changelog[512];		///< Human-readable release notes (may be empty)
};

// ─── Event callback ───────────────────────────────────────────────────────────

/**
 * FsotaEventCallback — registered via FsotaClient::onEvent().
 *
 * Stages:
 *   "CHECKING"    — filesystem version check initiated
 *   "DOWNLOADING" — image download started
 *   "VERIFYING"   — SHA-256 check in progress
 *   "INSTALLING"  — image being written to filesystem partition
 *   "COMPLETED"   — write succeeded; device should call ESP.restart() soon
 *   "FAILED"      — update aborted; see error parameter
 *   "NO_UPDATE"   — device already has the latest image
 *
 * @param stage    Stage name (never nullptr)
 * @param version  Target image version (never nullptr; may be empty on CHECKING)
 * @param error    Error description on FAILED stage; empty otherwise
 */
typedef void (*FsotaEventCallback)(const char *stage,
								   const char *version,
								   const char *error);

// ─── Helpers ─────────────────────────────────────────────────────────────────

/**
 * Return a short human-readable string for a FsotaResult code.
 * Stored in PROGMEM — safe to print from Serial.println().
 */
inline const __FlashStringHelper *fsotaResultStr(FsotaResult r)
{
	switch (r)
	{
		case FsotaResult::OK:           return F("OK");
		case FsotaResult::NO_UPDATE:    return F("NO_UPDATE");
		case FsotaResult::ERR_WIFI:     return F("ERR_WIFI");
		case FsotaResult::ERR_HTTP:     return F("ERR_HTTP");
		case FsotaResult::ERR_JSON:     return F("ERR_JSON");
		case FsotaResult::ERR_SHA256:   return F("ERR_SHA256");
		case FsotaResult::ERR_FLASH:    return F("ERR_FLASH");
		case FsotaResult::ERR_SIZE:     return F("ERR_SIZE");
		case FsotaResult::ERR_TOKEN:    return F("ERR_TOKEN");
		case FsotaResult::ERR_DOWNLOAD: return F("ERR_DOWNLOAD");
		case FsotaResult::ERR_ALLOC:    return F("ERR_ALLOC");
		default:                        return F("ERR_UNKNOWN");
	}
}

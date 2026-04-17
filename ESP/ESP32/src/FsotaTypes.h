#pragma once

/*
 * FsotaTypes.h — Shared data structures for the NodeWave Filesystem OTA (FSOTA) client.
 *
 * Included automatically by FsotaClient.h.
 */

#include <Arduino.h>

// ─── Result codes ─────────────────────────────────────────────────────────────

enum class FsotaResult : int8_t
{
	OK = 0,				 ///< Success — filesystem image applied
	NO_UPDATE = 1,		 ///< No newer filesystem image available
	ERR_WIFI = -1,		 ///< Network not connected
	ERR_HTTP = -2,		 ///< HTTP request failed
	ERR_JSON = -3,		 ///< Failed to parse server JSON response
	ERR_SHA256 = -4,	 ///< SHA-256 integrity check failed
	ERR_FLASH = -5,		 ///< Partition write/erase failed
	ERR_SIZE = -6,		 ///< Image exceeds partition size
	ERR_TOKEN = -7,		 ///< Auth token not configured
	ERR_PARTITION = -8,	 ///< Target filesystem partition not found
	ERR_DOWNLOAD = -9,	 ///< Download stream failed
	ERR_ALLOC = -10,	 ///< Memory allocation failed for chunk buffer
};

// ─── Filesystem OTA manifest ──────────────────────────────────────────────────

/**
 * FsotaManifest holds the decoded response from GET /api/v1/filesystem/check.
 */
struct FsotaManifest
{
	bool update_available;	 ///< True when a newer fs image exists
	char image_id[37];		 ///< UUIDv4 of the filesystem_images record
	char version[32];		 ///< Image version string (e.g. "1.2.3")
	char hardware_model[64]; ///< Hardware model
	char fs_type[16];		 ///< "LITTLEFS", "SPIFFS", "FATFS", or "CUSTOM"
	char hash[65];			 ///< SHA-256 hex digest (64 hex chars + NUL)
	char hash_algorithm[16]; ///< Always "sha256"
	uint32_t file_size;		 ///< Expected image size in bytes
	char download_url[1024]; ///< Short-lived signed download URL
	uint32_t expires_in;	 ///< URL validity in seconds (typically 300)
	char changelog[512];	 ///< Human-readable release notes (may be empty)
};

// ─── Callback ─────────────────────────────────────────────────────────────────

/**
 * Filesystem OTA lifecycle event callback — registered via FsotaClient::onEvent().
 *
 * Stages: "CHECKING", "DOWNLOADING", "VERIFYING", "INSTALLING",
 *         "COMPLETED", "FAILED", "NO_UPDATE"
 */
typedef void (*FsotaEventCallback)(const char *stage,
								   const char *version,
								   const char *error);

// ─── Human-readable result helper ─────────────────────────────────────────────

inline const char *fsotaResultStr(FsotaResult r)
{
	switch (r)
	{
	case FsotaResult::OK:			return "OK";
	case FsotaResult::NO_UPDATE:	return "NO_UPDATE";
	case FsotaResult::ERR_WIFI:		return "ERR_WIFI";
	case FsotaResult::ERR_HTTP:		return "ERR_HTTP";
	case FsotaResult::ERR_JSON:		return "ERR_JSON";
	case FsotaResult::ERR_SHA256:	return "ERR_SHA256";
	case FsotaResult::ERR_FLASH:	return "ERR_FLASH";
	case FsotaResult::ERR_SIZE:		return "ERR_SIZE";
	case FsotaResult::ERR_TOKEN:	return "ERR_TOKEN";
	case FsotaResult::ERR_PARTITION:return "ERR_PARTITION";
	case FsotaResult::ERR_DOWNLOAD:	return "ERR_DOWNLOAD";
	case FsotaResult::ERR_ALLOC:	return "ERR_ALLOC";
	default:						return "ERR_UNKNOWN";
	}
}

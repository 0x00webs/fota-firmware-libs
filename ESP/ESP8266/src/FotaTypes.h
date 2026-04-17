#pragma once
#include <Arduino.h>

/**
 * @file FotaTypes.h
 * @brief Shared enums, structs, callbacks, and helpers for the FOTA ESP8266 client.
 *
 * NOTE: No hardware-specific code in this file — same types as the ESP32 library.
 */

// ─── Result codes ─────────────────────────────────────────────────────────────

enum class FotaResult : int8_t
{
	OK = 0,				///< Success
	NO_UPDATE = 1,		///< No firmware update available
	ERR_WIFI = -1,		///< Network not connected
	ERR_HTTP = -2,		///< HTTP request failed (non-200 response, timeout, etc.)
	ERR_JSON = -3,		///< Failed to parse server JSON response
	ERR_SHA256 = -4,	///< SHA-256 integrity check failed
	ERR_SIGNATURE = -5, ///< Digital signature verification failed
	ERR_FLASH = -6,		///< OTA flash write / commit failed
	ERR_SIZE = -7,		///< Firmware exceeds FOTA_MAX_FIRMWARE_SIZE
	ERR_TOKEN = -8,		///< Auth token not configured
	ERR_PUBKEY = -9,	///< Public key not configured or fetch failed
	ERR_ALGO = -10,		///< Unsupported signature algorithm
	ERR_DOWNLOAD = -11, ///< Download URL fetch / redirect failed
	ERR_ALLOC = -12,	///< Memory allocation failed
	ERR_PROGRESS = -13, ///< Progress report HTTP failed (non-fatal — OTA continues)
	ERR_EXPIRED = -14,	///< Signed download URL has expired (403/410) — retried automatically
};

// ─── OTA manifest (decoded from /ota/check response) ──────────────────────────

/**
 * FotaManifest holds everything returned by GET /api/v1/ota/check.
 */
struct FotaManifest
{
	bool update_available;		  ///< True when a newer firmware exists
	char firmware_id[37];		  ///< UUIDv4 of the firmware record
	char version[32];			  ///< Firmware version string (e.g. "1.2.3")
	char hardware_model[64];	  ///< Hardware model string
	char hash[65];				  ///< SHA-256 hex digest (64 hex chars + NUL)
	char hash_algorithm[16];	  ///< Always "sha256"
	uint32_t file_size;			  ///< Expected binary size in bytes
	char signature[512];		  ///< Base64-encoded digital signature
	char signature_algorithm[16]; ///< "ECDSA_P256" or "RSA_SHA256" (no ED25519 on ESP8266)
	char public_key_id[17];		  ///< 16-char hex key fingerprint
	char download_url[1024];	  ///< Short-lived signed URL for the binary
	uint32_t expires_in;		  ///< URL validity in seconds (typically 300)
	char changelog[512];		  ///< Human-readable release notes (may be empty)
	char campaign_id[37];		  ///< UUID of the targeting campaign (empty if global)
	uint32_t check_interval_secs; ///< How often to re-check for updates (0 = use library default)
};

// ─── Public-key descriptor (from /firmware/public-key) ───────────────────────

struct FotaPublicKey
{
	char pem[2048];		///< PEM-encoded EC/RSA public key
	char key_id[17];	///< 16-char hex fingerprint
	char algorithm[16]; ///< "ECDSA_P256" or "RSA_SHA256"
};

// ─── Event callback ───────────────────────────────────────────────────────────

/**
 * OTA lifecycle event callback — registered via FotaClient::onEvent().
 *
 * Fired at every stage of performUpdate():
 *   "CHECKING"    — OTA check initiated
 *   "DOWNLOADING" — firmware binary download started
 *   "VERIFYING"   — SHA-256 + signature verification in progress
 *   "INSTALLING"  — binary is being written to the OTA partition
 *   "COMPLETED"   — flash succeeded; device should reboot soon
 *   "FAILED"      — update aborted; see error for the reason
 *   "NO_UPDATE"   — device is already on the latest firmware
 *
 * @param stage    Null-terminated stage name. Never nullptr.
 * @param version  Target firmware version string. Never nullptr.
 * @param error    Human-readable error description for FAILED; empty otherwise.
 */
typedef void (*FotaEventCallback)(const char *stage,
								  const char *version,
								  const char *error);

// ─── State machine ────────────────────────────────────────────────────────────

enum class FotaState : uint8_t
{
	IDLE = 0,		 ///< No activity; client is idle between updates
	CHECKING = 1,	 ///< Querying server for available firmware
	DOWNLOADING = 2, ///< Streaming firmware binary to OTA slot
	VERIFYING = 3,	 ///< SHA-256 integrity + signature verification
	INSTALLING = 4,	 ///< Committing verified binary to boot partition
	DONE = 5,		 ///< Update installed; device should reboot soon
	FAILED = 6,		 ///< Last update attempt failed
};

// ─── Diagnostic statistics ────────────────────────────────────────────────────

struct FotaStats
{
	uint32_t checkCount;   ///< Total performUpdate() / checkForUpdate() calls
	uint32_t updateCount;  ///< Successful firmware installs
	uint32_t failCount;	   ///< Failed attempts (any ERR_* except NO_UPDATE)
	uint32_t lastCheckMs;  ///< millis() value at the last check
	uint32_t lastUpdateMs; ///< millis() value at the last successful install
	FotaResult lastResult; ///< Result of the most recent performUpdate()
};

// ─── Human-readable result helper ─────────────────────────────────────────────

inline const char *fotaResultStr(FotaResult r)
{
	switch (r)
	{
	case FotaResult::OK:
		return "OK";
	case FotaResult::NO_UPDATE:
		return "No update available";
	case FotaResult::ERR_WIFI:
		return "WiFi not connected";
	case FotaResult::ERR_HTTP:
		return "HTTP request failed";
	case FotaResult::ERR_JSON:
		return "JSON parse error";
	case FotaResult::ERR_SHA256:
		return "SHA-256 mismatch";
	case FotaResult::ERR_SIGNATURE:
		return "Signature invalid";
	case FotaResult::ERR_FLASH:
		return "Flash write failed";
	case FotaResult::ERR_SIZE:
		return "Firmware too large";
	case FotaResult::ERR_TOKEN:
		return "Auth token not set";
	case FotaResult::ERR_PUBKEY:
		return "Public key missing";
	case FotaResult::ERR_ALGO:
		return "Unknown signature algorithm";
	case FotaResult::ERR_DOWNLOAD:
		return "Download failed";
	case FotaResult::ERR_ALLOC:
		return "Memory allocation failed";
	case FotaResult::ERR_PROGRESS:
		return "Progress report failed (non-fatal)";
	case FotaResult::ERR_EXPIRED:
		return "Download URL expired — retrying with fresh manifest";
	default:
		return "Unknown error";
	}
}

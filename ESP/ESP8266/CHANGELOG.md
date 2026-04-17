# Changelog — FOTA-Client-ESP8266

All notable changes to the ESP8266 FOTA client library are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

---

## [1.0.0] — 2026-03-10

### Added
- `FotaClient` class with full OTA pipeline: check → download → verify → flash.
- BearSSL-based TLS (via `BearSSL::WiFiClientSecure`) for secure API + download connections.
- `setCACert(pem)` — load a root CA trust anchor; falls back to `setInsecure()` when `nullptr`.
- `setClientCert(pem)` / `setClientKey(pem)` — mutual TLS (mTLS) support.
- SHA-256 firmware integrity verification (`FOTA_VERIFY_SHA256`).
- Digital signature verification: **ECDSA P-256** and **RSA-SHA256** (`FOTA_VERIFY_SIGNATURE`).
  - Note: Ed25519 is **not** supported on ESP8266 (SDK mbedTLS limitation).
- `onEvent(cb)` — lifecycle callback fired at every OTA stage.
- Configurable retry with backoff (`FOTA_RETRY_COUNT`, `FOTA_RETRY_DELAY_MS`).
- Auto-fetch public key (`FOTA_AUTO_FETCH_PUBLIC_KEY`).
- Campaign-aware OTA: `setDeviceId()` / `FOTA_DEVICE_ID`.
- Progress reporting via `POST /ota/device/progress` (`FOTA_REPORT_PROGRESS`).
- `checkIntervalSecs()` — returns server-provided polling interval.
- `getState()` / `getStats()` / `resetStats()` — diagnostics.
- `FOTA_REBOOT_ON_SUCCESS` — auto-restart after successful flash.
- `FOTA_WATCHDOG_FEED` — `yield()` during download chunks to prevent SWDT resets.
- `BasicUpdate.ino` example sketch.
- `library.json`, `library.properties`, `platformio.ini`, `keywords.txt`.

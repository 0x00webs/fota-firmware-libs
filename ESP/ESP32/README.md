# FOTA-Client-ESP32

**Secure, production-ready Firmware Over-The-Air (FOTA) client for the ESP32 family.**

FOTA-Client-ESP32 connects your device to the NodeWave FOTA platform, checks for firmware
updates, downloads and cryptographically verifies them, and installs them via the native
`esp_https_ota` subsystem — all from a single `performUpdate()` call.

| | |
|---|---|
| **Version** | 1.2.0 |
| **Author** | NodeWave \<dev@nodewave.io\> |
| **License** | MIT |
| **Architecture** | ESP32 (all variants) |
| **Framework** | Arduino (arduino-esp32 ≥ 2.0) |
| **Dependency** | [ArduinoJson](https://arduinojson.org/) ≥ 7.0 |

---

## Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
  - [Arduino IDE 1.x](#arduino-ide-1x--library-manager)
  - [Arduino IDE 2.x](#arduino-ide-2x--library-manager)
  - [PlatformIO CLI](#platformio-cli)
  - [VSCode + PlatformIO IDE Extension](#vscode--platformio-ide-extension)
  - [Manual Installation](#manual-installation)
- [Quick Start](#quick-start)
- [Platform Configuration](#platform-configuration)
- [Configuration Reference](#configuration-reference)
  - [§ 1 — Server](#-1--server)
  - [§ 2 — Device Identity](#-2--device-identity)
  - [§ 3 — Authentication](#-3--authentication)
  - [§ 4 — TLS / Server Certificate](#-4--tls--server-certificate)
  - [§ 5 — Firmware Verification](#-5--firmware-verification)
  - [§ 6 — Progress Reporting](#-6--progress-reporting)
  - [§ 7 — Networking](#-7--networking)
  - [§ 8 — Behaviour](#-8--behaviour)
  - [§ 9 — Logging](#-9--logging)
  - [§ 10 — SD Card Temp Storage](#-10--sd-card-temp-storage-optional)
  - [§ 11 — Post-Boot Health Watchdog](#-11--post-boot-health-watchdog-optional)
  - [§ 12 — WiFi Reconnect](#-12--wifi-reconnect)
- [API Reference](#api-reference)
  - [FotaClient](#fotaclient)
  - [DeviceAuth](#deviceauth)
  - [DevicePKI](#devicepki)
  - [FotaVerify](#fotaverify)
  - [Types and Enumerations](#types-and-enumerations)
- [Examples](#examples)
  - [1 — Minimal (development / CI)](#1--minimal--development--ci)
  - [2 — Production (CA cert + embedded public key)](#2--production--ca-cert--embedded-public-key)
  - [3 — DeviceAuth (per-device JWT from NVS)](#3--deviceauth--per-device-jwt-from-nvs)
  - [4 — DevicePKI (mTLS provisioning)](#4--devicepki--mtls-provisioning)
  - [5 — Lifecycle callback (LED progress indicator)](#5--lifecycle-callback--led-progress-indicator)
  - [6 — Periodic OTA with server-supplied interval](#6--periodic-ota-with-server-supplied-interval)
  - [7 — SD Card temp storage](#7--sd-card-temp-storage)
  - [8 — Post-boot health watchdog](#8--post-boot-health-watchdog)
  - [9 — PlatformIO OTA upgrade chain (v1 to v5)](#9--platformio-ota-upgrade-chain-v1-to-v5)
- [PlatformIO Board Environments](#platformio-board-environments)
- [Security](#security)
- [Troubleshooting](#troubleshooting)
- [Changelog](#changelog)

---

## Features

- **One-call OTA** — `performUpdate()` handles the full check → download → verify → flash pipeline
- **SHA-256 integrity** — every binary is hash-verified before any flash write
- **Digital signature verification** — ECDSA P-256, Ed25519, and RSA-SHA256 via mbedTLS (bundled with ESP-IDF)
- **TLS server authentication** — root CA pinning; optional mutual TLS (mTLS)
- **Automatic retry** — configurable back-off for transient network failures; security failures are never retried
- **Lifecycle callbacks** — `onEvent()` fires at every stage (CHECKING → DOWNLOADING → VERIFYING → INSTALLING → COMPLETED) for LEDs, displays, MQTT, telemetry
- **Campaign-aware OTA** — per-device targeting via `FOTA_DEVICE_ID`; server returns campaign-specific firmware
- **Progress reporting** — automatic `POST /ota/device/progress` to the platform dashboard
- **Per-device JWT authentication** — `DeviceAuth` manages device credentials and token caching in NVS
- **PKI / mTLS provisioning** — `DevicePKI` generates ECDSA key pairs, builds CSRs, and provisions signed certificates
- **OTA rollback support** — confirms the running partition via `esp_ota_mark_app_valid_cancel_rollback()`
- **Post-boot health watchdog** — optional deferred partition confirmation with automatic rollback on deadline expiry
- **SD card temp storage** — optional two-pass update: download → SD card → verify → flash (no full-firmware RAM allocation required)
- **Server-driven poll interval** — `checkIntervalSecs()` returns the backend's recommended polling cadence
- **PSRAM-aware** — firmware download buffer uses PSRAM when available
- **Watchdog-safe** — download loop yields to feed the ESP-IDF task WDT

---

## Requirements

### Hardware
- Any ESP32 variant: ESP32, ESP32-S3, ESP32-S2, ESP32-C3, ESP32-C6, ESP32-H2, or ESP32-C2

### Software

| Dependency | Version | Notes |
|---|---|---|
| [arduino-esp32](https://github.com/espressif/arduino-esp32) | ≥ 2.0 | ESP-IDF ≥ 4.4; ESP-IDF 5.x recommended for Ed25519 |
| [ArduinoJson](https://arduinojson.org/) | ≥ 7.0 | Install via Library Manager |

### Platform
A running NodeWave FOTA backend instance accessible over HTTPS.

---

## Installation

### Arduino IDE 1.x — Library Manager

1. Open **Sketch → Include Library → Manage Libraries…**
2. In the search box type **FOTA-Client-ESP32**
3. Select the entry by *NodeWave* and click **Install**
4. When prompted, also install the **ArduinoJson** dependency (≥ 7.0)

### Arduino IDE 2.x — Library Manager

1. Click the **Library Manager** icon in the left sidebar (or press `Ctrl+Shift+I`)
2. Search for **FOTA-Client-ESP32**
3. Click **Install** next to the *NodeWave* entry
4. Click **Install All** to include the ArduinoJson dependency automatically

### PlatformIO CLI

Add both dependencies to your project's `platformio.ini`:

```ini
[env:your_board]
platform  = espressif32
framework = arduino
board     = esp32dev          ; replace with your target board

lib_deps =
    nodewave/FOTA-Client-ESP32 @ ^1.2.0
    bblanchon/ArduinoJson      @ ^7.0.0
```

Then build:

```bash
pio run
```

### VSCode + PlatformIO IDE Extension

1. Install the **PlatformIO IDE** extension from the VSCode Extensions Marketplace
   (extension ID: `platformio.platformio-ide`)
2. Open (or create) a PlatformIO project — **PIO Home → New Project**, choose your board
   and the *Arduino* framework
3. Open `platformio.ini` and add under your `[env:…]` section:

   ```ini
   lib_deps =
       nodewave/FOTA-Client-ESP32 @ ^1.2.0
       bblanchon/ArduinoJson      @ ^7.0.0
   ```

4. Save the file — PlatformIO automatically resolves and downloads the libraries
5. Verify the installation by adding `#include <FotaClient.h>` to your sketch and clicking
   **Build** (✓) in the PlatformIO toolbar

> **Tip:** You can also install the library globally via PIO Home → Libraries search panel
> and clicking **Add to Project**.

### Manual Installation

1. Download or clone this repository
2. **Arduino IDE:** Copy the entire folder into your Arduino `libraries/` directory
   (typically `~/Arduino/libraries/` on Linux/macOS or `Documents\Arduino\libraries\` on Windows)
3. **PlatformIO:** Copy the folder into your project's `lib/` directory (or any path listed
   in `lib_extra_dirs`)
4. Restart the Arduino IDE (PlatformIO picks up `lib/` changes automatically)

---

## Quick Start

```cpp
// 1. Define configuration BEFORE the #include
#define FOTA_SERVER_URL       "https://fota.mycompany.com"
#define FOTA_HARDWARE_MODEL   "ESP32-WROOM-32"
#define FOTA_CURRENT_VERSION  "1.0.0"
#define FOTA_AUTH_TOKEN       "fota_d_<your-device-api-key>"
#define FOTA_DEVICE_ID        "my-device-001"
#define FOTA_VERIFY_SIGNATURE 0   // set to 1 with a public key in production

#include <FotaClient.h>
#include <WiFi.h>

FotaClient fota;

size_t getArduinoLoopTaskStackSize() { return 32 * 1024; }

void setup() {
    Serial.begin(115200);
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    fota.begin();
    fota.setCACert(nullptr);   // pass your root CA PEM in production

    FotaResult result = fota.performUpdate();

    if (result == FotaResult::OK) {
        delay(2000);
        ESP.restart();
    } else if (result != FotaResult::NO_UPDATE) {
        Serial.printf("OTA failed: %s\n", fota.lastError());
    }
}

void loop() { delay(60000); }
```

> **Important:** All `#define` configuration macros **must** appear **before**
> `#include <FotaClient.h>`. The header reads them at inclusion time via preprocessor
> guards. Defining them after the include has no effect.

---

## Configuration Reference

Configuration is controlled entirely via `#define` macros placed in your sketch before
`#include <FotaClient.h>`. Every macro has a safe default so only the values you need to
change require explicit definitions. You never need to edit library sources.

**Sketch pattern:**

```cpp
// my_sketch.ino — put these ABOVE #include <FotaClient.h>
#define FOTA_SERVER_URL      "https://fota.mycompany.com"
#define FOTA_HARDWARE_MODEL  "ESP32-S3"
#define FOTA_CURRENT_VERSION "2.1.0"
// ... other overrides ...
#include <FotaClient.h>
```

**PlatformIO build_flags alternative** — use this so the library translation unit also
sees the macros:

```ini
build_flags =
    -DFOTA_VERIFY_SIGNATURE=0
    '-DFOTA_CURRENT_VERSION="2.1.0"'
    -DFOTA_LOG_LEVEL=4
```

---

### § 1 — Server

| Macro | Default | Description |
|---|---|---|
| `FOTA_SERVER_URL` | `"https://api.example.com"` | Base URL of your FOTA backend — **no trailing slash**. The only macro that is truly required in every production sketch. |
| `FOTA_API_PREFIX` | `"/api/v1"` | API path prefix prepended to every endpoint. Must match the `globalPrefix` configured on the NestJS backend. |

---

### § 2 — Device Identity

| Macro | Default | Description |
|---|---|---|
| `FOTA_HARDWARE_MODEL` | `"ESP32-WROOM-32"` | Hardware model string. Must exactly match the `hardware_model` column in the platform's `firmwares` table. Case-sensitive. |
| `FOTA_CURRENT_VERSION` | `"0.0.0"` | SemVer string of the firmware **currently running** on the device. No leading `v`. The server compares this against available firmware versions to decide whether an update exists. |

---

### § 3 — Authentication

| Macro | Default | Description |
|---|---|---|
| `FOTA_AUTH_TOKEN` | `""` | Bearer token sent in every API request's `Authorization: Bearer …` header. Obtain from Dashboard → Devices → API Key (format `fota_d_<hex>`). **Leave empty and call `setAuthToken()` at runtime** to avoid embedding credentials in compiled firmware (recommended for production). |
| `FOTA_DEVICE_ID` | `""` | Device business-key registered in the FOTA platform. When set, `GET /ota/check` includes `?device_id=<id>` so the server returns campaign-targeted firmware ahead of the global latest release. **Leave empty and call `setDeviceId()` at runtime** when the ID is derived from the MAC address or NVS. |

---

### § 4 — TLS / Server Certificate

| Macro | Default | Description |
|---|---|---|
| `FOTA_SERVER_CA_CERT` | `nullptr` | PEM-encoded root CA certificate for TLS server authentication. **`nullptr` disables TLS peer verification** — never use in production (MITM attacks will succeed silently). The library emits a `log_w()` warning when `nullptr` is active. |

**Obtaining your CA certificate:**

```bash
openssl s_client -connect api.example.com:443 -showcerts 2>/dev/null \
  | openssl x509 -outform PEM
```

**Embedding in your sketch:**

```cpp
#define FOTA_SERVER_CA_CERT           \
  "-----BEGIN CERTIFICATE-----\n"    \
  "MIIBxTCCAW+gAwIBAgIJAP...\n"     \
  "-----END CERTIFICATE-----\n"
```

---

### § 5 — Firmware Verification

| Macro | Default | Description |
|---|---|---|
| `FOTA_VERIFY_SHA256` | `1` | Enable SHA-256 integrity verification before flashing. `1` = enabled (default), `0` = disabled. **Disable only during development.** A mismatch returns `ERR_SHA256` and is never retried. |
| `FOTA_VERIFY_SIGNATURE` | `1` | Enable digital signature verification. `1` = enabled (default), `0` = disabled. **Always `1` in production.** Supported algorithms: `ECDSA_P256`, `RSA_SHA256`, `ED25519`. A mismatch returns `ERR_SIGNATURE` and is never retried. |
| `FOTA_SIGNING_PUBLIC_KEY` | `""` | PEM-encoded public key for signature verification. **Embed at compile time for the strongest security guarantee** — the key cannot be swapped at runtime. Leave as `""` and rely on `FOTA_AUTO_FETCH_PUBLIC_KEY` if you prefer server-side key management. |
| `FOTA_AUTO_FETCH_PUBLIC_KEY` | `1` | When `1` and `FOTA_SIGNING_PUBLIC_KEY` is empty, the client automatically calls `GET /api/v1/firmware/public-key` the first time an update manifest is received and caches the PEM in RAM for the session. Requires `FOTA_SERVER_CA_CERT` — fetching a key over an unverified TLS connection is blocked. |

**Obtaining the signing public key manually:**

```bash
curl -H "Authorization: Bearer <token>" \
     https://api.example.com/api/v1/firmware/public-key | jq -r .public_key_pem
```

**Embedding the public key:**

```cpp
#define FOTA_SIGNING_PUBLIC_KEY            \
  "-----BEGIN PUBLIC KEY-----\n"           \
  "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcD...\n" \
  "-----END PUBLIC KEY-----\n"
```

---

### § 6 — Progress Reporting

| Macro | Default | Description |
|---|---|---|
| `FOTA_REPORT_PROGRESS` | `1` | Automatically POST OTA lifecycle stages to `POST /ota/device/progress` at each step: `DOWNLOADING`, `VERIFYING`, `INSTALLING`, `COMPLETED`, `FAILED`. Requires `FOTA_DEVICE_ID`; silently no-ops if it is empty. Progress POST failures are **non-fatal** and never abort the update. `1` = enabled (default), `0` = disabled. |

---

### § 7 — Networking

| Macro | Default | Description |
|---|---|---|
| `FOTA_HTTP_TIMEOUT_MS` | `30000` | HTTP receive timeout (ms) for short JSON API calls. Does not apply to the firmware binary download. |
| `FOTA_CONNECT_TIMEOUT_MS` | `10000` | TCP connect + TLS handshake timeout (ms). Tune upward on congested IoT networks where TLS negotiation can stall. |
| `FOTA_DOWNLOAD_TIMEOUT_MS` | `90000` | Maximum time allowed to stream the complete firmware binary (ms). Set significantly higher on cellular or LPWAN links. |
| `FOTA_MAX_FIRMWARE_SIZE` | `4194304` (4 MiB) | Maximum firmware binary size in bytes. Payloads larger than this are rejected immediately with `ERR_SIZE`. Reduce to `2 * 1024 * 1024` on memory-constrained variants (ESP32-H2, ESP32-C2). |
| `FOTA_URL_EXPIRY_MARGIN_S` | `30` | Safety margin (seconds) before treating a signed download URL as expired. Effective lifetime = `manifest.expires_in − FOTA_URL_EXPIRY_MARGIN_S`. |
| `FOTA_RETRY_COUNT` | `3` | Maximum retries on transient failures. **Security failures and configuration errors are never retried.** |
| `FOTA_RETRY_DELAY_MS` | `5000` | Delay between retry attempts (ms). Also overridable at runtime with `setRetryDelay()`. |
| `FOTA_WIFI_RECONNECT_TIMEOUT_MS` | `30000` | Maximum time (ms) to wait for WiFi to reconnect before starting a retry attempt. |
| `FOTA_USER_AGENT` | `"FotaClient-ESP32/1.2.0 (arduino-esp32)"` | HTTP `User-Agent` header sent with every API and download request. |

---

### § 8 — Behaviour

| Macro | Default | Description |
|---|---|---|
| `FOTA_REBOOT_ON_SUCCESS` | `0` | When `1`, `performUpdate()` automatically calls `esp_restart()` on a successful flash. When `0` (default), `FotaResult::OK` signals the caller to reboot at a convenient time. |
| `FOTA_ROLLBACK_ENABLED` | `1` | When `1` (default), `begin()` calls `esp_ota_mark_app_valid_cancel_rollback()` immediately, confirming the running OTA partition. **Leave `1` unless you manage OTA confirmation explicitly** via the health watchdog. |
| `FOTA_WATCHDOG_FEED` | `1` | When `1` (default), the firmware download loop yields after each read chunk to feed the ESP-IDF task watchdog timer. Prevents WDT resets during long downloads. |

---

### § 9 — Logging

| Macro | Default | Description |
|---|---|---|
| `FOTA_LOG_LEVEL` | `3` | Controls ESP-IDF log verbosity for the `FotaClient` component. Applied via `esp_log_level_set()` inside `FotaClient::begin()`. `0` = silent, `1` = errors only, `2` = errors + warnings, `3` = errors + warnings + info *(default)*, `4` = full debug. |

---

### § 10 — SD Card Temp Storage (optional)

These macros are active only when `FOTA_USE_SD_TEMP 1`. The default single-pass stream mode requires no SD card.

| Macro | Default | Description |
|---|---|---|
| `FOTA_USE_SD_TEMP` | `0` | Enable SD card intermediate storage. When `1`, the update uses a two-pass flow: **Pass 1** — HTTP → SD file (SHA-256 computed incrementally during download). **Pass 2** — verify SHA-256 + signature from the SD file *before* touching the OTA partition, then flash from SD. `0` = stream directly to OTA partition *(default)*. |
| `FOTA_SD_TEMP_PATH` | `"/fota_tmp.bin"` | Absolute path of the temp firmware file on the SD filesystem. Must start with `/`. Any pre-existing file at this path is overwritten. |
| `FOTA_SD_FS` | `SD` | SD filesystem object to use. Default is `SD` (SPI library). For SDMMC set `#define FOTA_SD_FS SD_MMC` and `#include <SD_MMC.h>`. |
| `FOTA_RESUME_SD_DOWNLOAD` | `1` | When `1` (default), a failed download is resumed using an HTTP `Range:` header instead of restarting from byte 0. Set `0` to always restart the download. |

**Benefits of SD card mode:**
- SHA-256 and signature are fully verified **before** the OTA partition is written.
- Download can be retried without re-issuing `esp_ota_begin()`, reducing SPI flash write cycles.
- On boards without PSRAM (e.g. ESP32-C2), only a 4 KB read buffer is needed in RAM.

**Requirements:** Initialize the SD filesystem (`SD.begin()` or `SD_MMC.begin()`) **before** calling `performUpdate()`.

**Runtime override:**

```cpp
fota.setSDTempPath("/ota/firmware_update.bin");
```

---

### § 11 — Post-Boot Health Watchdog (optional)

| Macro | Default | Description |
|---|---|---|
| `FOTA_HEALTH_TIMEOUT_MS` | `0` | Time window (ms) in which the sketch must call `FotaClient::markHealthy()` after `begin()` on a freshly-flashed OTA partition. When `0` (default), `begin()` immediately confirms the partition. Requires `FOTA_ROLLBACK_ENABLED 1`. |

**Recommended value:** `300000` (5 minutes).

**Workflow when `FOTA_HEALTH_TIMEOUT_MS > 0`:**

1. New firmware boots → `begin()` detects `PENDING_VERIFY` partition → arms the deadline → `isHealthPending()` returns `true`.
2. The sketch performs its own health checks (WiFi up, sensors responding, server reachable, etc.).
3. On success → call `markHealthy()`. This calls `esp_ota_mark_app_valid_cancel_rollback()`.
4. Call `tick()` from `loop()`. If `markHealthy()` is **not** called within `FOTA_HEALTH_TIMEOUT_MS` ms, `tick()` invokes `esp_ota_mark_app_invalid_rollback_and_reboot()` and the device reboots into the previous (known-good) firmware.

```cpp
#define FOTA_HEALTH_TIMEOUT_MS 300000  // 5 minutes

void setup() {
    fota.begin();   // arms the watchdog deadline
    // ... WiFi connect, sensor init ...
    if (allSystemsGo()) {
        fota.markHealthy();  // confirm firmware is healthy
    }
}

void loop() {
    fota.tick();  // triggers rollback if deadline expires
}
```

---

### § 12 — WiFi Reconnect

| Macro | Default | Description |
|---|---|---|
| `FOTA_WIFI_RECONNECT_TIMEOUT_MS` | `30000` | Maximum time (ms) to wait for WiFi to reconnect before a retry is started. Set `0` to disable the reconnect wait and retry immediately. |

---

## API Reference

### FotaClient

The main OTA client class. Include with `#include <FotaClient.h>`.

#### Constructor

```cpp
FotaClient fota;
```

#### Initialisation

```cpp
void begin(
    const char *serverUrl      = FOTA_SERVER_URL,
    const char *hardwareModel  = FOTA_HARDWARE_MODEL,
    const char *currentVersion = FOTA_CURRENT_VERSION,
    const char *authToken      = FOTA_AUTH_TOKEN,
    const char *deviceId       = FOTA_DEVICE_ID
);
```

Initialises the client. Call once in `setup()` after WiFi is connected. All parameters default
to their corresponding `FotaConfig.h` macros. When `FOTA_ROLLBACK_ENABLED == 1` (and
`FOTA_HEALTH_TIMEOUT_MS == 0`), also confirms the running OTA partition immediately.

---

#### Authentication

```cpp
void setAuthToken(const char *token);
```
Override the Bearer token at runtime. Must be called before `performUpdate()` if `FOTA_AUTH_TOKEN` was left empty.

```cpp
void setDeviceId(const char *deviceId);
```
Override the device business-key at runtime. When set, `/ota/check` requests include
`?device_id=<id>` for campaign-aware firmware targeting.

---

#### TLS Configuration

```cpp
void setCACert(const char *pem);
```
Set the root CA certificate (PEM) to verify the FOTA server's TLS certificate.
Pass `nullptr` to skip peer verification — **insecure; development only.**

```cpp
void setClientCert(const char *pem);
void setClientKey(const char *pem);
```
Enable mutual TLS (mTLS). Both PEM strings are held **by pointer** — ensure the backing
buffers remain valid for the entire lifetime of the `FotaClient` instance.

---

#### Signature Verification

```cpp
void setPublicKey(const char *pem);
```
Set the PEM public key for firmware signature verification. Overrides `FOTA_SIGNING_PUBLIC_KEY`.

```cpp
FotaResult fetchPublicKey(const char *expectedKeyId = nullptr);
```
Fetch the signing public key from `GET /api/v1/firmware/public-key` and store it in RAM.
Optionally pin the key by passing the expected 16-character hex fingerprint as `expectedKeyId`.
Requires `setCACert()` with a valid certificate.

---

#### Core API

```cpp
FotaResult checkForUpdate(FotaManifest &manifest);
```
Query the server for an available firmware update.

| Return value | Meaning |
|---|---|
| `FotaResult::OK` | Update available; `manifest` is fully populated |
| `FotaResult::NO_UPDATE` | Device is already on the latest firmware |
| `FotaResult::ERR_*` | Error; inspect `lastError()` |

```cpp
FotaResult performUpdate();
```
**One-shot helper:** check → download → verify → flash, with automatic retry.

**Pipeline per attempt:**

| Step | Action |
|---|---|
| 1 | `checkForUpdate()` — `GET /api/v1/ota/check` |
| 2 | `fetchPublicKey()` — auto-fetch if `FOTA_AUTO_FETCH_PUBLIC_KEY == 1` and key not cached |
| 3 | Download firmware binary (streaming, PSRAM-aware; or SD card if `FOTA_USE_SD_TEMP 1`) |
| 4 | `fotaVerifySha256()` — SHA-256 integrity check |
| 5 | `fotaVerifySignature()` — digital signature verification (if `FOTA_VERIFY_SIGNATURE == 1`) |
| 6 | `esp_https_ota()` — write verified binary to the OTA partition |

Transient failures are retried up to `FOTA_RETRY_COUNT` times. Security failures
(`ERR_SHA256`, `ERR_SIGNATURE`) and configuration errors (`ERR_TOKEN`, `ERR_PUBKEY`) are
**never retried**.

| Return value | Meaning |
|---|---|
| `FotaResult::OK` | Firmware flashed; call `ESP.restart()` unless `FOTA_REBOOT_ON_SUCCESS == 1` |
| `FotaResult::NO_UPDATE` | Device is already on the latest firmware |
| `FotaResult::ERR_*` | Failure after all retries exhausted |

---

#### Runtime Overrides

```cpp
void onEvent(FotaEventCallback cb);
```
Register a lifecycle event callback. Fired at every stage of `performUpdate()`:

| Stage | Fired when |
|---|---|
| `"CHECKING"` | OTA check request initiated |
| `"DOWNLOADING"` | Firmware binary download started |
| `"VERIFYING"` | Hash + signature verification in progress |
| `"INSTALLING"` | Verified binary being written to OTA partition |
| `"COMPLETED"` | Flash succeeded; device should reboot |
| `"FAILED"` | Update aborted; `error` parameter contains the reason |
| `"NO_UPDATE"` | Device already on the latest firmware |

```cpp
void setRetryCount(uint8_t count);   // Override FOTA_RETRY_COUNT at runtime
void setRetryDelay(uint32_t ms);     // Override FOTA_RETRY_DELAY_MS at runtime
void setSDTempPath(const char *path); // Override FOTA_SD_TEMP_PATH (FOTA_USE_SD_TEMP only)
```

---

#### State and Diagnostics

```cpp
FotaState   getState() const;
bool        isUpdating() const;
const char *lastError() const;
uint32_t    checkIntervalSecs() const;
const FotaStats &getStats() const;
void        resetStats();
```

`checkIntervalSecs()` returns the server-supplied recommended polling cadence (populated
from `manifest.check_interval_secs`). Returns `0` before the first successful check — use
your own default in that case.

**Typical `loop()` usage:**

```cpp
static uint32_t lastCheckMs = 0;
uint32_t intervalMs = fota.checkIntervalSecs()
                        ? fota.checkIntervalSecs() * 1000UL
                        : 24UL * 3600UL * 1000UL;  // fallback: 24 h
if (millis() - lastCheckMs >= intervalMs) {
    lastCheckMs = millis();
    fota.performUpdate();
}
```

```cpp
FotaResult reportProgress(const char *targetVersion,
                           const char *status,
                           const char *errorMsg = nullptr);
```
Manually report OTA progress to the platform. Called automatically by `performUpdate()`;
exposed publicly for advanced step-by-step update flows. Always returns `FotaResult::OK`.

---

#### Post-Boot Health Watchdog

```cpp
void markHealthy();
bool isHealthPending() const;
void tick();
```

| Method | Description |
|---|---|
| `markHealthy()` | Confirm the new firmware is healthy. Calls `esp_ota_mark_app_valid_cancel_rollback()` and disarms the deadline. |
| `isHealthPending()` | `true` when a freshly-flashed `PENDING_VERIFY` partition has not yet been confirmed. |
| `tick()` | Enforce the health watchdog deadline from `loop()`. Triggers rollback if the deadline expires. |

Only meaningful when `FOTA_HEALTH_TIMEOUT_MS > 0`.

---

### DeviceAuth

Per-device JWT authentication helper. Manages the full credential lifecycle: authenticate,
cache the JWT in NVS, and transparently re-authenticate on expiry.

Include with `#include <DeviceAuth.h>`.

#### Configuration Macros

| Macro | Default | Description |
|---|---|---|
| `DEVICE_AUTH_SERVER_URL` | `FOTA_SERVER_URL` | FOTA backend base URL. |
| `DEVICE_AUTH_DEVICE_ID` | `""` | Device business-key. Must match `device_id` in the platform's `devices` table. |
| `DEVICE_AUTH_DEVICE_SECRET` | `""` | Raw 64-hex-char device secret from Dashboard → Devices → Credentials. **Never log or expose this value.** |
| `DEVICE_AUTH_API_PREFIX` | `FOTA_API_PREFIX` | API path prefix. |
| `DEVICE_AUTH_NVS_NS` | `"fota_auth"` | NVS (Preferences) namespace for the cached JWT. |
| `DEVICE_AUTH_REAUTH_MARGIN` | `86400` | Re-authenticate this many seconds before the JWT actually expires. Default: 86400 (1 day). |

#### Methods

```cpp
void setServerUrl(const char *url);
void setApiPrefix(const char *prefix);
void setDeviceId(const char *deviceId);
void setDeviceSecret(const char *secret);
void setCACert(const char *pem);
```

```cpp
bool ensureValid();
```
Ensure a valid JWT is available. Steps: (1) load from NVS; (2) if absent or expiring soon,
call `authenticate()`; (3) save to NVS. Returns `true` if a valid token is available. Call
in `setup()` after WiFi is connected, before `fota.begin()`.

```cpp
bool authenticate();
```
Force a fresh authentication request. Returns `true` on success.

```cpp
template <typename FotaClientT>
void applyTo(FotaClientT &fota);
```
Inject the cached JWT into a `FotaClient` instance via `fota.setAuthToken()`.

```cpp
const char *getToken() const;        // Cached JWT string (empty if not authenticated)
bool        isTokenValid() const;    // true if token is non-empty AND not expired
uint32_t    getTokenExpiry() const;  // Unix timestamp when the token expires (0 if unknown)
bool        loadFromNVS();
void        saveToNVS();
void        clearNVS();
const char *lastError() const;
```

---

### DevicePKI

ECDSA P-256 key generation, PKCS#10 CSR building, server-side CSR signing, and mTLS wiring.
All credentials are persisted in NVS (private key + certificate).

Include with `#include <DevicePKI.h>`.

#### Configuration Macro

| Macro | Default | Description |
|---|---|---|
| `DEVICE_PKI_NVS_NS` | `"fota_pki"` | NVS namespace for `key_pem` and `cert_pem`. |

#### PKIResult

```cpp
enum class PKIResult : int8_t {
    OK          =  0,   // Operation succeeded
    ERR_KEYGEN  = -1,   // mbedTLS key generation failed
    ERR_CSR     = -2,   // CSR generation failed
    ERR_NETWORK = -3,   // HTTP transport error
    ERR_SERVER  = -4,   // Server rejected the CSR (4xx/5xx)
    ERR_NVS     = -5,   // NVS read/write failure
    ERR_NO_CERT = -6,   // No certificate available in NVS
};

const char *pkiResultStr(PKIResult r);
```

#### Methods

```cpp
PKIResult generateKeyPair(bool forceRegenerate = false);
```
Generate an ECDSA P-256 key pair and persist the private key to NVS. Safe to call on every
boot (no-op if a key already exists). Pass `forceRegenerate = true` after certificate
revocation.

```cpp
PKIResult generateCSR(const char *deviceId, char *outPem, size_t outLen);
```
Build a PEM-encoded PKCS#10 CSR signed with the stored private key. `outLen` ≥ 1024 bytes.

```cpp
PKIResult submitCSR(const char *serverUrl,
                    const char *deviceDbId,
                    const char *operatorJwt,
                    const char *csrPem,
                    const char *caCert = nullptr);
```
POST the CSR to `POST /api/v1/pki/devices/<deviceDbId>/csr`. `operatorJwt` is required only
once during initial provisioning. On success, the signed certificate is stored in NVS.

```cpp
PKIResult provision(const char *serverUrl,
                    const char *deviceDbId,
                    const char *operatorJwt,
                    const char *caCert = nullptr,
                    bool forceReprovision = false);
```
**Full provisioning in one call:** `generateKeyPair()` → `generateCSR()` → `submitCSR()`.
Skips steps 2–3 if `hasValidCert()` is `true`. Pass `forceReprovision = true` to rotate
the certificate.

```cpp
bool      hasValidCert() const;  // true if cert_pem exists in NVS
void      clearCredentials();    // Erases key_pem and cert_pem from NVS
PKIResult applyTo(FotaClient &fota);
```

`applyTo()` loads key + cert from NVS and calls `fota.setClientKey()` +
`fota.setClientCert()`, enabling mTLS. The `DevicePKI` instance must remain alive as long
as the `FotaClient` is used. Returns `ERR_NO_CERT` if credentials are not in NVS.

---

### FotaVerify

Low-level cryptographic verification functions used internally by `FotaClient`.

Include with `#include <FotaVerify.h>`.

```cpp
int fotaVerifySha256(const uint8_t *data, size_t len, const char *expected_hex);
```
Compute SHA-256 and compare against a 64-character lowercase hex string. Returns `0` on
match, `-1` on mismatch.

```cpp
int fotaVerifySha256Digest(const uint8_t hash32[32], const char *expected_hex);
```
Compare an already-computed 32-byte binary SHA-256 digest against `expected_hex`.

```cpp
int fotaVerifySignature(const uint8_t  *hash32,
                        const char     *sig_base64,
                        const char     *pubkey_pem,
                        const char     *algorithm);
```
Verify a Base64-encoded digital signature over the 32-byte binary SHA-256 digest.
`algorithm` must be one of `"ECDSA_P256"`, `"RSA_SHA256"`, or `"ED25519"`. Returns `0` on
valid signature, `-1` on failure.

```cpp
int fotaBase64Decode(const char *in, uint8_t *out, size_t out_max, size_t *out_len);
```
Decode a NUL-terminated Base64 string. Returns `0` on success, `-1` on error.

---

### Types and Enumerations

#### FotaResult

```cpp
enum class FotaResult : int8_t {
    OK            =  0,   // Success
    NO_UPDATE     =  1,   // No firmware update available
    ERR_WIFI      = -1,   // WiFi not connected
    ERR_HTTP      = -2,   // HTTP request failed (non-200 response, timeout, etc.)
    ERR_JSON      = -3,   // Failed to parse server JSON response
    ERR_SHA256    = -4,   // SHA-256 integrity check failed          — never retried
    ERR_SIGNATURE = -5,   // Digital signature verification failed   — never retried
    ERR_FLASH     = -6,   // esp_https_ota flash write failed
    ERR_SIZE      = -7,   // Firmware exceeds FOTA_MAX_FIRMWARE_SIZE
    ERR_TOKEN     = -8,   // Auth token not configured               — never retried
    ERR_PUBKEY    = -9,   // Public key missing or fetch failed      — never retried
    ERR_ALGO      = -10,  // Unsupported signature algorithm         — never retried
    ERR_DOWNLOAD  = -11,  // Download URL fetch / redirect failed
    ERR_ALLOC     = -12,  // Memory allocation failed (OOM)
    ERR_PROGRESS  = -13,  // Progress report POST failed (non-fatal)
    ERR_EXPIRED   = -14,  // Signed download URL expired             — retried automatically
};

const char *fotaResultStr(FotaResult r);
```

> **Retry behaviour:** `ERR_WIFI`, `ERR_HTTP`, `ERR_JSON`, `ERR_DOWNLOAD`, `ERR_FLASH`,
> `ERR_ALLOC`, and `ERR_EXPIRED` are retried up to `FOTA_RETRY_COUNT` times.
> All codes annotated *never retried* abort immediately.

---

#### FotaState

```cpp
enum class FotaState : uint8_t {
    IDLE        = 0,   // No activity
    CHECKING    = 1,   // Querying server for available firmware
    DOWNLOADING = 2,   // Streaming firmware binary
    VERIFYING   = 3,   // SHA-256 + signature verification
    INSTALLING  = 4,   // Writing verified binary to OTA partition
    DONE        = 5,   // Update installed; device should reboot
    FAILED      = 6,   // Last update attempt failed
};
```

---

#### FotaManifest

Populated by `checkForUpdate()`. All field names mirror the `/api/v1/ota/check` JSON response.

```cpp
struct FotaManifest {
    bool     update_available;        // true when a newer firmware exists
    char     firmware_id[37];         // UUIDv4 of the firmware record
    char     version[32];             // Target version string (e.g. "1.2.3")
    char     hardware_model[64];      // Hardware model string
    char     hash[65];                // SHA-256 hex digest (64 chars + NUL)
    char     hash_algorithm[16];      // Always "sha256"
    uint32_t file_size;               // Expected binary size in bytes
    char     signature[512];          // Base64-encoded digital signature
    char     signature_algorithm[16]; // "ECDSA_P256", "ED25519", or "RSA_SHA256"
    char     public_key_id[17];       // 16-char hex key fingerprint
    char     download_url[1024];      // Short-lived signed download URL
    uint32_t expires_in;              // URL validity window in seconds
    char     changelog[512];          // Human-readable release notes (may be empty)
    char     campaign_id[37];         // UUID of targeting campaign (empty if global)
    uint32_t check_interval_secs;     // Server-recommended polling interval (0 = use default)
};
```

---

#### FotaPublicKey

```cpp
struct FotaPublicKey {
    char pem[2048];      // PEM-encoded public key
    char key_id[17];     // 16-char hex fingerprint
    char algorithm[16];  // "ECDSA_P256", "ED25519", or "RSA_SHA256"
};
```

---

#### FotaStats

Diagnostic counters accumulated from boot (or last `resetStats()`).

```cpp
struct FotaStats {
    uint32_t   checkCount;    // Total performUpdate() / checkForUpdate() calls
    uint32_t   updateCount;   // Successful firmware installs
    uint32_t   failCount;     // Failed attempts (any ERR_* result)
    uint32_t   lastCheckMs;   // millis() at the last check
    uint32_t   lastUpdateMs;  // millis() at the last successful install
    FotaResult lastResult;    // Result of the most recent performUpdate()
};
```

---

#### FotaEventCallback

```cpp
typedef void (*FotaEventCallback)(
    const char *stage,    // Stage name (see onEvent() table above)
    const char *version,  // Target firmware version (current version for CHECKING/NO_UPDATE)
    const char *error     // Error description; empty string for non-FAILED stages
);
```

All three parameters are always non-`nullptr`. Copy string values if you need to retain them
beyond the callback invocation.

---

## Examples

### 1 — Minimal — development / CI

Quickest way to verify the FOTA pipeline without TLS or signature verification.
**Do not use this pattern in production.**

```cpp
#define FOTA_SERVER_URL       "https://fota.mycompany.com"
#define FOTA_HARDWARE_MODEL   "ESP32-WROOM-32"
#define FOTA_CURRENT_VERSION  "1.0.0"
#define FOTA_AUTH_TOKEN       "fota_d_0a97d50a..."
#define FOTA_DEVICE_ID        "test-device-01"
#define FOTA_VERIFY_SIGNATURE 0
#define FOTA_LOG_LEVEL        4  // full debug output

#include <FotaClient.h>
#include <WiFi.h>

FotaClient fota;

size_t getArduinoLoopTaskStackSize() { return 32 * 1024; }

void setup() {
    Serial.begin(115200);
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    fota.begin();
    fota.setCACert(nullptr);

    FotaResult r = fota.performUpdate();
    if (r == FotaResult::OK) { delay(2000); ESP.restart(); }
    else if (r != FotaResult::NO_UPDATE)
        Serial.printf("OTA failed [%s]: %s\n", fotaResultStr(r), fota.lastError());
}

static uint32_t lastCheckMs = 0;

void loop() {
    uint32_t intervalMs = fota.checkIntervalSecs()
                            ? fota.checkIntervalSecs() * 1000UL
                            : 7UL * 24UL * 3600UL * 1000UL;
    if (millis() - lastCheckMs >= intervalMs) {
        lastCheckMs = millis();
        FotaResult r = fota.performUpdate();
        if (r == FotaResult::OK) { delay(2000); ESP.restart(); }
    }
    delay(1000);
}
```

---

### 2 — Production — CA cert + embedded public key

Full security: TLS server verification, SHA-256 integrity, ECDSA P-256 signature.

```cpp
#define FOTA_SERVER_URL      "https://fota.mycompany.com"
#define FOTA_HARDWARE_MODEL  "ESP32-S3"
#define FOTA_CURRENT_VERSION "2.0.0"
#define FOTA_AUTH_TOKEN      "fota_d_<device-api-key>"
#define FOTA_DEVICE_ID       "prod-device-abc"

#define FOTA_SERVER_CA_CERT              \
  "-----BEGIN CERTIFICATE-----\n"       \
  "MIIB...your CA cert content...\n"    \
  "-----END CERTIFICATE-----\n"

#define FOTA_SIGNING_PUBLIC_KEY              \
  "-----BEGIN PUBLIC KEY-----\n"             \
  "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcD...\n"  \
  "-----END PUBLIC KEY-----\n"

#include <FotaClient.h>
#include <WiFi.h>

FotaClient fota;

size_t getArduinoLoopTaskStackSize() { return 32 * 1024; }

void setup() {
    Serial.begin(115200);
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    fota.begin();
    fota.setCACert(FOTA_SERVER_CA_CERT);

    FotaResult r = fota.performUpdate();
    if (r == FotaResult::OK) { delay(2000); ESP.restart(); }
    else if (r != FotaResult::NO_UPDATE)
        Serial.printf("OTA failed: %s\n", fota.lastError());
}
```

---

### 3 — DeviceAuth — per-device JWT from NVS

Authenticates each device individually with its own secret.

```cpp
#define FOTA_SERVER_URL             "https://fota.mycompany.com"
#define FOTA_HARDWARE_MODEL         "ESP32-S3"
#define FOTA_CURRENT_VERSION        "1.0.0"
#define DEVICE_AUTH_DEVICE_ID       "esp32-sensor-kitchen"
#define DEVICE_AUTH_DEVICE_SECRET   "abcdef01234567..."  // 64 hex chars

#include <DeviceAuth.h>
#include <FotaClient.h>
#include <WiFi.h>

DeviceAuth auth;
FotaClient fota;

size_t getArduinoLoopTaskStackSize() { return 32 * 1024; }

void setup() {
    Serial.begin(115200);
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    auth.setCACert(nullptr);  // add your CA cert in production

    if (!auth.ensureValid()){
        Serial.printf("Auth failed: %s\n", auth.lastError());
        return;
    }
    auth.applyTo(fota);

    fota.begin();
    FotaResult r = fota.performUpdate();
    if (r == FotaResult::OK) { delay(2000); ESP.restart(); }
}
```

---

### 4 — DevicePKI — mTLS provisioning

One-time provisioning of a device certificate; subsequent FOTA sessions use mutual TLS.

```cpp
#define FOTA_SERVER_URL      "https://fota.mycompany.com"
#define FOTA_HARDWARE_MODEL  "ESP32-S3"
#define FOTA_CURRENT_VERSION "1.0.0"
#define OPERATOR_JWT         "eyJhbGci..."
#define DEVICE_DB_UUID       "550e8400-e29b-41d4-a716-446655440000"

#include <DevicePKI.h>
#include <FotaClient.h>
#include <WiFi.h>

DevicePKI pki;
FotaClient fota;

size_t getArduinoLoopTaskStackSize() { return 32 * 1024; }

void setup() {
    Serial.begin(115200);
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    // provision() is a no-op on subsequent boots if cert already in NVS
    PKIResult pr = pki.provision(FOTA_SERVER_URL, DEVICE_DB_UUID, OPERATOR_JWT);
    if (pr != PKIResult::OK) {
        Serial.printf("PKI failed: %s\n", pkiResultStr(pr));
        return;
    }

    pki.applyTo(fota);
    fota.begin();
    fota.setCACert(nullptr);  // add your CA cert in production

    FotaResult r = fota.performUpdate();
    if (r == FotaResult::OK) { delay(2000); ESP.restart(); }
}
```

---

### 5 — Lifecycle callback — LED progress indicator

```cpp
#define FOTA_SERVER_URL       "https://fota.mycompany.com"
#define FOTA_HARDWARE_MODEL   "ESP32-WROOM-32"
#define FOTA_CURRENT_VERSION  "1.0.0"
#define FOTA_AUTH_TOKEN       "fota_d_..."
#define FOTA_VERIFY_SIGNATURE 0

#include <FotaClient.h>
#include <WiFi.h>

#define LED_PIN 2
FotaClient fota;

void onFotaEvent(const char *stage, const char *version, const char *error) {
    if      (strcmp(stage, "CHECKING")    == 0) Serial.printf("[FOTA] Checking v%s\n", version);
    else if (strcmp(stage, "DOWNLOADING") == 0) { Serial.printf("[FOTA] Downloading v%s\n", version); digitalWrite(LED_PIN, HIGH); }
    else if (strcmp(stage, "VERIFYING")   == 0) Serial.printf("[FOTA] Verifying v%s\n", version);
    else if (strcmp(stage, "INSTALLING")  == 0) Serial.printf("[FOTA] Installing v%s\n", version);
    else if (strcmp(stage, "COMPLETED")   == 0) { Serial.printf("[FOTA] v%s installed!\n",version);digitalWrite(LED_PIN,LOW);}
    else if (strcmp(stage, "FAILED")      == 0) { Serial.printf("[FOTA] FAILED: %s\n", error); digitalWrite(LED_PIN, LOW); }
    else if (strcmp(stage, "NO_UPDATE")   == 0) Serial.printf("[FOTA] Up to date v%s\n", version);
}

size_t getArduinoLoopTaskStackSize() { return 32 * 1024; }

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    fota.onEvent(onFotaEvent);
    fota.begin();
    fota.setCACert(nullptr);

    FotaResult r = fota.performUpdate();
    if (r == FotaResult::OK) { delay(2000); ESP.restart(); }
}

static uint32_t lastCheckMs = 0;

void loop() {
    uint32_t intervalMs = fota.checkIntervalSecs() ? fota.checkIntervalSecs() * 1000UL : 3600UL * 1000UL;
    if (millis() - lastCheckMs >= intervalMs) {
        lastCheckMs = millis();
        FotaResult r = fota.performUpdate();
        if (r == FotaResult::OK) { delay(2000); ESP.restart(); }
    }
    delay(1000);
}
```

---

### 6 — Periodic OTA with server-supplied interval

```cpp
static uint32_t lastCheckMs = 0;

void loop() {
    const uint32_t intervalSecs = fota.checkIntervalSecs() > 0
                                    ? fota.checkIntervalSecs()
                                    : 24UL * 3600UL;  // fallback: 24 hours
    if (millis() - lastCheckMs >= intervalSecs * 1000UL) {
        lastCheckMs = millis();
        FotaResult r = fota.performUpdate();
        if (r == FotaResult::OK) {
            delay(2000);
            ESP.restart();
        } else if (r != FotaResult::NO_UPDATE) {
            Serial.printf("OTA error: %s\n", fota.lastError());
            const FotaStats &s = fota.getStats();
            Serial.printf("checks=%u  updates=%u  fails=%u\n",
                          s.checkCount, s.updateCount, s.failCount);
        }
    }
    delay(1000);
}
```

---

### 7 — SD Card temp storage

```cpp
#define FOTA_SERVER_URL       "https://fota.mycompany.com"
#define FOTA_HARDWARE_MODEL   "ESP32-WROOM-32"
#define FOTA_CURRENT_VERSION  "1.0.0"
#define FOTA_AUTH_TOKEN       "fota_d_..."
#define FOTA_VERIFY_SIGNATURE 0
#define FOTA_USE_SD_TEMP      1
#define FOTA_SD_TEMP_PATH     "/ota_fw.bin"

#include <FotaClient.h>
#include <SD.h>
#include <WiFi.h>

FotaClient fota;

size_t getArduinoLoopTaskStackSize() { return 32 * 1024; }

void setup() {
    Serial.begin(115200);
    if (!SD.begin(5/*CSpin*/)){
        Serial.println("SD mount failed!");
        return;
    }
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    fota.begin();
    fota.setCACert(nullptr);

    FotaResult r = fota.performUpdate();
    if (r == FotaResult::OK) { delay(2000); ESP.restart(); }
    else if (r != FotaResult::NO_UPDATE)
        Serial.printf("OTA failed: %s\n", fota.lastError());
}
```

---

### 8 — Post-boot health watchdog

```cpp
#define FOTA_SERVER_URL         "https://fota.mycompany.com"
#define FOTA_HARDWARE_MODEL     "ESP32-S3"
#define FOTA_CURRENT_VERSION    "3.0.0"
#define FOTA_AUTH_TOKEN         "fota_d_..."
#define FOTA_VERIFY_SIGNATURE   0
#define FOTA_ROLLBACK_ENABLED   1
#define FOTA_HEALTH_TIMEOUT_MS  300000   // 5 minutes

#include <FotaClient.h>
#include <WiFi.h>

FotaClient fota;

size_t getArduinoLoopTaskStackSize() { return 32 * 1024; }

void setup() {
    Serial.begin(115200);
    fota.begin();  // arms the health watchdog FIRST — before WiFi

    WiFi.begin("SSID", "password");
    for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) delay(500);

    if (WiFi.status() == WL_CONNECTED) {
        fota.markHealthy();      // disarm the watchdog
        fota.setCACert(nullptr);
        FotaResult r = fota.performUpdate();
        if (r == FotaResult::OK) { delay(2000); ESP.restart(); }
    }
    // If markHealthy() is never called, tick() triggers rollback after timeout
}

void loop() {
    fota.tick();  // enforces the health watchdog deadline
    delay(1000);
}
```

---

### 9 — PlatformIO OTA upgrade chain (v1 to v5)

The repository includes five test sketches (`examples/FirmwareV1` through `FirmwareV5`)
and matching PlatformIO environments for end-to-end OTA chain testing.

**Workflow:**

1. Flash `firmware_v1` as the starting firmware:
   ```bash
   pio run -e firmware_v1 -t upload
   pio device monitor
   ```
2. Build `firmware_v2` and upload the `.bin` to the FOTA dashboard as version `2.0.0`:
   ```bash
   pio run -e firmware_v2
   # Upload .pio/build/firmware_v2/firmware.bin via Dashboard → Firmwares → Upload
   ```
3. Create a campaign targeting your device. The device running v1 automatically downloads
   v2.0.0 and reboots.
4. Repeat for v3, v4, v5.

Each test binary uses `FOTA_VERIFY_SIGNATURE=0` for speed. Enable signature verification
and embed the public key for production builds.

---

## PlatformIO Board Environments

The library ships with a `platformio.ini` covering all supported ESP32 variants.

| Environment | Board | Notes |
|---|---|---|
| `esp32dev` | ESP32 (WROOM / WROVER / generic LX6) | Default; 4 MiB flash, SPI OTA |
| `esp32s3` | ESP32-S3 DevKitC-1 (LX7) | Built-in USB-CDC; add `-DARDUINO_USB_CDC_ON_BOOT=1` |
| `esp32c3` | ESP32-C3 DevKitM-1 (RISC-V) | Ultra-low-power; `upload_protocol = esptool` |
| `esp32s2` | ESP32-S2 Saola-1 (LX7) | USB OTG, single-core; `upload_protocol = esp-builtin` |
| `esp32c6` | ESP32-C6 DevKitC-1 (RISC-V) | Wi-Fi 6 + BLE 5; `upload_protocol = esptool` |
| `esp32h2` | ESP32-H2 DevKitM-1 (RISC-V) | No Wi-Fi — requires IP gateway; reduce `FOTA_MAX_FIRMWARE_SIZE` to 2 MiB |
| `esp32c2` | ESP8684 DevKitM-1 (RISC-V) | Ultra-budget, 272 KB RAM; consider `FOTA_USE_SD_TEMP 1` if heap is tight |
| `firmware_v1` … `firmware_v5` | ESP32-S3 DevKitC-1 | OTA upgrade chain test binaries; signature verification disabled |

**Common build flags:**

```ini
build_flags =
    -DFOTA_LOG_LEVEL=3
    -DCORE_DEBUG_LEVEL=3
    -DARDUINO_USB_CDC_ON_BOOT=1          ; ESP32-S3 only
    -DFOTA_VERIFY_SIGNATURE=0            ; development only
    '-DFOTA_CURRENT_VERSION="1.0.0"'
```

**Back-trace decoder:**

```ini
monitor_filters = esp32_exception_decoder
```

---

## Security

### Production Checklist

| Item | Recommended setting | Risk if ignored |
|---|---|---|
| TLS server verification | `FOTA_SERVER_CA_CERT` — embed root CA PEM | MITM attack; attacker serves arbitrary firmware |
| SHA-256 integrity | `FOTA_VERIFY_SHA256 1` (default) | Corrupted binary could brick the device |
| Signature verification | `FOTA_VERIFY_SIGNATURE 1` (default) | Unauthenticated firmware can be pushed |
| Public key source | Embed at compile time via `FOTA_SIGNING_PUBLIC_KEY` | Key fetched over network can be substituted |
| Auth token | Per-device keys (`fota_d_...`) over shared operator tokens | One compromised token exposes all devices |
| mTLS | Provision device certificates via `DevicePKI` | Server cannot authenticate individual devices |
| Post-boot validation | Use `FOTA_HEALTH_TIMEOUT_MS` + `markHealthy()` | Defective firmware stays installed permanently |

### Security Properties

- **Integrity** — every binary is SHA-256 hashed before any flash write; a mismatch aborts
  immediately and is **never retried**. The OTA partition is not touched on a hash failure.
- **Authenticity** — ECDSA P-256 / Ed25519 / RSA-SHA256 signature verified via mbedTLS
  (bundled with ESP-IDF). No external crypto library required.
- **Confidentiality** — all API and download traffic is HTTPS. TLS 1.2+ only.
- **Rollback safety** — `esp_ota_mark_app_valid_cancel_rollback()` is called in `begin()`.
  If the new firmware panics before reaching `begin()`, the ESP32 bootloader automatically
  rolls back to the previous (known-good) partition on the next WDT reset.
- **Auto-fetch key protection** — `FOTA_AUTO_FETCH_PUBLIC_KEY 1` is silently blocked when
  `FOTA_SERVER_CA_CERT nullptr` is active. An unauthenticated key fetch would undermine the
  entire signature verification chain.

### Known Limitations

- When `FOTA_SERVER_CA_CERT nullptr` is used, TLS peer verification is **disabled** and the
  library emits a `log_w()` warning. Intentional for development; must never reach production.
- Signed download URLs must have sufficient validity (`expires_in` ≥ 300 s recommended).

---

## Troubleshooting

### `ERR_TOKEN` — Auth token not configured

Ensure `FOTA_AUTH_TOKEN` is defined **before** `#include <FotaClient.h>`, or call
`fota.setAuthToken()` **before** `fota.begin()`. Defining the macro after the include has
no effect.

### `ERR_HTTP` on `performUpdate()`

- Confirm WiFi is connected before `fota.begin()`.
- Check `FOTA_SERVER_URL` has no trailing slash.
- Verify the backend is reachable: `curl https://fota.mycompany.com/health`
- Enable full debug logging: `#define FOTA_LOG_LEVEL 4`
- If using a reverse proxy or Cloudflare Tunnel, confirm the `Authorization` header is not
  being stripped.

### `ERR_SHA256` or `ERR_SIGNATURE`

- The firmware binary on the server may have been corrupted or re-uploaded after signing.
- The public key embedded in the sketch does not match the key used to sign the firmware.
- These errors are **never retried** — fix the server-side firmware record and trigger a
  fresh `performUpdate()`.

### `ERR_PUBKEY` — Public key missing

- If `FOTA_SIGNING_PUBLIC_KEY` is empty, ensure `FOTA_AUTO_FETCH_PUBLIC_KEY 1` (default)
  **and** that `setCACert()` is called with a valid CA certificate. Auto-fetch is blocked
  when TLS verification is disabled.
- Alternatively, call `fota.fetchPublicKey()` explicitly before `performUpdate()`.

### OTA rollback after flashing / boots to previous firmware

- `FOTA_ROLLBACK_ENABLED 1` requires `fota.begin()` to be called early in `setup()` of the
  **new** firmware.
- If `begin()` is never reached (panic, assertion, early return), the bootloader rolls back
  on the next WDT reset — this is the intended safety behaviour.
- If using `FOTA_HEALTH_TIMEOUT_MS > 0`, confirm `markHealthy()` is called and `tick()` is
  called from `loop()`.

### Download stalls or times out

- Increase `FOTA_DOWNLOAD_TIMEOUT_MS` for slow or cellular links (try `180000`).
- Increase `FOTA_CONNECT_TIMEOUT_MS` on networks with slow TLS negotiation.

### `ERR_EXPIRED` — Download URL expired

The signed download URL expired between the `/ota/check` call and the download attempt.
The library retries automatically with a fresh manifest. If retries are exhausted, increase
`FOTA_RETRY_COUNT` or reduce `FOTA_URL_EXPIRY_MARGIN_S`. Verify the device clock is broadly
correct (NTP).

### `ERR_ALLOC` — Memory allocation failed

- Check `ESP.getFreeHeap()` before calling `performUpdate()`.
- On boards without PSRAM, enable SD card mode: `#define FOTA_USE_SD_TEMP 1`.
- Ensure PSRAM is enabled in the board definition for boards that have it.

### Serial output on two COM ports (ESP32-S3 with USB-CDC)

Add to `[env:esp32s3]` in `platformio.ini`:

```ini
build_flags = -DARDUINO_USB_CDC_ON_BOOT=1
```

### Stack overflow or watchdog reset during TLS handshake

Increase the Arduino loop task stack size:

```cpp
size_t getArduinoLoopTaskStackSize() { return 32 * 1024; }
```

mbedTLS requires approximately 20 KB for a TLS 1.3 handshake; 32 KB provides comfortable
headroom. The default Arduino loop stack on ESP-IDF 5.x is 8 KB — insufficient for TLS.

### DeviceAuth: `authenticate()` returns false

- Confirm `DEVICE_AUTH_DEVICE_SECRET` is the raw 64-hex-char secret from Dashboard →
  Devices → Credentials.
- Confirm `device_id` matches exactly (case-sensitive).
- Inspect `auth.lastError()` for the HTTP status code and error detail.

### DevicePKI: `provision()` returns `ERR_SERVER`

- The `operatorJwt` may have expired. Refresh the token and re-run provisioning.
- Verify `deviceDbId` is the UUID from the FOTA platform device registry — not the
  business-key `device_id`.

---

## Changelog

### [1.2.0] — 2026-03-08

**Added**
- Post-boot health watchdog: `FOTA_HEALTH_TIMEOUT_MS`, `markHealthy()`, `tick()`, `isHealthPending()`
- SD card temp storage: `FOTA_USE_SD_TEMP`, `FOTA_SD_TEMP_PATH`, `FOTA_SD_FS`, `FOTA_RESUME_SD_DOWNLOAD`, `setSDTempPath()`
- WiFi reconnect wait: `FOTA_WIFI_RECONNECT_TIMEOUT_MS`
- `FotaResult::ERR_EXPIRED` — expired signed URL retried automatically with fresh manifest
- `FotaManifest::check_interval_secs` + `FotaClient::checkIntervalSecs()`
- `FotaClient::reportProgress()` exposed publicly
- `FotaPublicKey` struct
- mTLS support: `setClientCert()` / `setClientKey()` + `DevicePKI` class
- `DeviceAuth` class — per-device JWT authentication with NVS caching
- Board environments: `esp32c6`, `esp32h2`, `esp32c2`

### [1.1.0] — 2026-02-11

**Added**
- Campaign-aware OTA via `FOTA_DEVICE_ID` / `setDeviceId()`
- Live progress reporting to the FOTA platform (`FOTA_REPORT_PROGRESS`)
- `fetchPublicKey()` — runtime signing key retrieval with optional key-id pinning
- PSRAM-aware firmware download buffer
- `FOTA_URL_EXPIRY_MARGIN_S`
- `FotaManifest::campaign_id`
- Full mbedTLS signature rewrite: ECDSA P-256, RSA-SHA256, Ed25519

### [1.0.0] — 2026-02-10

Initial release.
- SHA-256 firmware integrity check
- ECDSA P-256 signature verification via mbedTLS
- TLS root CA pinning
- `begin()` + `performUpdate()` one-shot API
- `FotaConfig.h` compile-time configuration
- `FotaState` enum + `FotaStats` diagnostics
- `onEvent()` lifecycle callback
- Configurable retry with back-off
- `FOTA_AUTO_FETCH_PUBLIC_KEY`, `FOTA_REBOOT_ON_SUCCESS`, `FOTA_LOG_LEVEL`

---

## License

MIT © 2026 NodeWave \<dev@nodewave.io\>

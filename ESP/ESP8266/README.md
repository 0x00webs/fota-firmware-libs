# FOTA-Client-ESP8266

**Secure, production-ready Firmware Over-The-Air (FOTA) client for the ESP8266.**

FOTA-Client-ESP8266 connects your ESP8266 to the NodeWave FOTA platform, checks for firmware
updates, downloads and cryptographically verifies them, and installs them via the Arduino
`Updater` — all from a single `performUpdate()` call.

| | |
|---|---|
| **Version** | 1.0.0 |
| **Author** | NodeWave \<dev@nodewave.io\> |
| **License** | MIT |
| **Architecture** | ESP8266 |
| **Framework** | Arduino (arduino-esp8266 ≥ 3.1) |
| **Dependency** | [ArduinoJson](https://arduinojson.org/) ≥ 7.0 |

---

## Table of Contents

- [Features](#features)
- [ESP8266-Specific Notes](#esp8266-specific-notes)
- [Requirements](#requirements)
- [Installation](#installation)
  - [Arduino IDE 1.x](#arduino-ide-1x--library-manager)
  - [Arduino IDE 2.x](#arduino-ide-2x--library-manager)
  - [PlatformIO CLI](#platformio-cli)
  - [VSCode + PlatformIO IDE Extension](#vscode--platformio-ide-extension)
  - [Manual Installation](#manual-installation)
- [Quick Start](#quick-start)
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
- [API Reference](#api-reference)
  - [FotaClient](#fotaclient)
  - [FotaVerify](#fotaverify)
  - [Types and Enumerations](#types-and-enumerations)
- [Examples](#examples)
  - [1 — Minimal (development)](#1--minimal-development)
  - [2 — Production (CA cert + embedded public key)](#2--production-ca-cert--embedded-public-key)
  - [3 — mTLS with client certificate](#3--mtls-with-client-certificate)
  - [4 — Lifecycle callback (LED progress)](#4--lifecycle-callback-led-progress)
  - [5 — Periodic OTA with server-supplied interval](#5--periodic-ota-with-server-supplied-interval)
- [PlatformIO Board Environments](#platformio-board-environments)
- [Security](#security)
- [Troubleshooting](#troubleshooting)
- [Changelog](#changelog)

---

## Features

- **One-call OTA** — `performUpdate()` handles the full check → download → verify → flash pipeline
- **SHA-256 integrity** — every binary is hash-verified before any flash write
- **Digital signature verification** — ECDSA P-256 and RSA-SHA256 via mbedTLS (bundled with ESP8266 SDK)
- **TLS server authentication** — root CA trust anchor via BearSSL; optional mutual TLS (mTLS)
- **Automatic retry** — configurable back-off for transient failures; security failures are never retried
- **Lifecycle callbacks** — `onEvent()` fires at every stage (CHECKING → DOWNLOADING → VERIFYING → INSTALLING → COMPLETED)
- **Campaign-aware OTA** — per-device targeting via `FOTA_DEVICE_ID`
- **Progress reporting** — automatic `POST /ota/device/progress` to the platform dashboard
- **Server-driven poll interval** — `checkIntervalSecs()` returns the backend's recommended cadence
- **Watchdog-safe** — download loop calls `yield()` every chunk to feed the software WDT

---

## ESP8266-Specific Notes

This library targets the ESP8266 and differs from **FOTA-Client-ESP32** in several important ways:

| Aspect | ESP8266 | ESP32 |
|---|---|---|
| TLS stack | BearSSL (`BearSSL::WiFiClientSecure`) | mbedTLS (`WiFiClientSecure`) |
| HTTP client | `ESP8266HTTPClient` | `HTTPClient` |
| OTA flash engine | Arduino `Updater` (`Update.h`) | `esp_https_ota` |
| Signature algorithms | ECDSA P-256, RSA-SHA256 | + Ed25519 |
| OTA rollback | ❌ Not available | ✅ `esp_ota_mark_app_valid_cancel_rollback` |
| Max firmware (default) | 1 MiB (`FOTA_MAX_FIRMWARE_SIZE`) | 4 MiB |
| Download chunk size | 512 bytes | 4096 bytes |
| TLS connect timeout | 15 s (BearSSL handshake is slower) | 10 s |
| Logging | `Serial.printf` | `esp_log` |

**Ed25519 is not supported.** The ESP8266 SDK ships an older mbedTLS that does not include the
Ed25519 curve. If the server sends an Ed25519-signed manifest the library will return
`FOTA_ERR_VERIFY_FAILED` and log an explicit error — no silent skip.

**No OTA rollback.** The Arduino `Updater` on ESP8266 does not support partition-level rollback.
Design your firmware so that new versions can be detected as faulty and re-flashed externally
if necessary.

**BearSSL heap pressure.** BearSSL allocates ~6 KB on the stack during the TLS handshake.
Ensure your sketch leaves at least 16 KB of free heap before calling `performUpdate()`.
Use `ESP.getFreeHeap()` to verify.

---

## Requirements

### Hardware
- ESP8266, ESP8266EX, or any module based on it (D1 Mini, NodeMCU, Wemos D1, etc.)
- Flash ≥ 2 MB (1 MB for sketch + 1 MB OTA partition) recommended

### Software

| Dependency | Version | Notes |
|---|---|---|
| [arduino-esp8266](https://github.com/esp8266/Arduino) | ≥ 3.1.0 | Includes BearSSL and Updater |
| [ArduinoJson](https://arduinojson.org/) | ≥ 7.0 | Install via Library Manager |

### Platform
A running NodeWave FOTA backend instance accessible over HTTPS.

---

## Installation

### Arduino IDE 1.x — Library Manager

1. Open **Sketch → Include Library → Manage Libraries…**
2. Search for **FOTA-Client-ESP8266**
3. Select the entry by *NodeWave* and click **Install**
4. Install the **ArduinoJson** dependency (≥ 7.0) when prompted

### Arduino IDE 2.x — Library Manager

1. Click the **Library Manager** icon (`Ctrl+Shift+I`)
2. Search for **FOTA-Client-ESP8266**
3. Click **Install** → **Install All** to include ArduinoJson

### PlatformIO CLI

```ini
[env:d1_mini]
platform  = espressif8266
framework = arduino
board     = d1_mini

lib_deps =
    nodewave/FOTA-Client-ESP8266 @ ^1.0.0
    bblanchon/ArduinoJson        @ ^7.0.0

build_flags = -D BEARSSL_SSL_BASIC
```

```bash
pio run
```

> **`BEARSSL_SSL_BASIC`** strips cipher suites the FOTA server does not use, saving ~10 KB flash.
> Remove the flag only if you need compatibility with servers that require legacy suites.

### VSCode + PlatformIO IDE Extension

1. Install the **PlatformIO IDE** extension (`platformio.platformio-ide`)
2. Open your project and edit `platformio.ini` as shown above
3. Save — PlatformIO resolves and downloads the libraries automatically
4. Add `#include <FotaClient.h>` and click **Build**

### Manual Installation

1. Download the latest release ZIP from GitHub
2. Arduino IDE: **Sketch → Include Library → Add .ZIP Library…**
3. PlatformIO: extract into your project's `lib/` directory

---

## Quick Start

```cpp
#include <ESP8266WiFi.h>
#include <FotaClient.h>

// Root CA of your FOTA backend (PEM format, stored in flash)
static const char ROOT_CA[] PROGMEM = R"(
-----BEGIN CERTIFICATE-----
...your CA certificate...
-----END CERTIFICATE-----
)";

FotaClient fota;

void setup() {
    Serial.begin(115200);
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    fota.setServerUrl("https://fota.example.com");
    fota.setDeviceId("esp8266-001");
    fota.setCurrentVersion("1.0.0");
    fota.setCACert(ROOT_CA);

    fota.onEvent([](FotaEvent ev, const FotaClient *c) {
        Serial.printf("[FOTA] event=%d state=%d\n", (int)ev, (int)c->getState());
    });

    FotaResult r = fota.performUpdate();
    if (r == FOTA_OK) {
        Serial.println("Update applied — rebooting");
        ESP.restart();
    } else if (r == FOTA_NO_UPDATE) {
        Serial.println("Firmware is up to date");
    } else {
        Serial.printf("FOTA failed: %s\n", fotaResultStr(r));
    }
}

void loop() {}
```

---

## Configuration Reference

Override any `#define` **before** including `FotaClient.h`:

```cpp
#define FOTA_SERVER_URL "https://fota.example.com"
#include <FotaClient.h>
```

Or in PlatformIO via `build_flags`:

```ini
build_flags =
    -D FOTA_SERVER_URL=\"https://fota.example.com\"
    -D FOTA_DEVICE_ID=\"esp8266-001\"
```

---

### § 1 — Server

| Macro | Default | Description |
|---|---|---|
| `FOTA_SERVER_URL` | `""` | Base URL of the FOTA backend (no trailing slash) |
| `FOTA_API_CHECK_PATH` | `"/ota/device/check"` | Check-for-update endpoint |
| `FOTA_API_PUBLIC_KEY_PATH` | `"/ota/device/public-key"` | Public key fetch endpoint |
| `FOTA_API_PROGRESS_PATH` | `"/ota/device/progress"` | Progress report endpoint |

### § 2 — Device Identity

| Macro | Default | Description |
|---|---|---|
| `FOTA_DEVICE_ID` | `""` | Unique device identifier sent in every request |
| `FOTA_CURRENT_VERSION` | `""` | Currently running firmware version (semver) |

### § 3 — Authentication

| Macro | Default | Description |
|---|---|---|
| `FOTA_API_KEY` | `""` | Static API key sent as `X-API-Key` header |
| `FOTA_DEVICE_TOKEN` | `""` | Per-device bearer token |

### § 4 — TLS / Server Certificate

| Macro | Default | Description |
|---|---|---|
| `FOTA_CA_CERT` | `nullptr` | PEM root CA. If `nullptr` the connection is insecure (dev only) |
| `FOTA_CLIENT_CERT` | `nullptr` | PEM client certificate for mTLS |
| `FOTA_CLIENT_KEY` | `nullptr` | PEM client private key for mTLS |

> ⚠️ **Never** ship devices with `FOTA_CA_CERT nullptr` in production. An attacker on the same
> network can serve arbitrary firmware.

### § 5 — Firmware Verification

| Macro | Default | Description |
|---|---|---|
| `FOTA_VERIFY_SHA256` | `1` | Enable SHA-256 hash check (strongly recommended) |
| `FOTA_VERIFY_SIGNATURE` | `0` | Enable signature verification (requires public key) |
| `FOTA_PUBLIC_KEY_PEM` | `nullptr` | Embedded public key PEM |
| `FOTA_AUTO_FETCH_PUBLIC_KEY` | `1` | Fetch public key from server when none embedded |
| `FOTA_MAX_FIRMWARE_SIZE` | `1048576` | Maximum accepted firmware size in bytes (1 MiB) |

### § 6 — Progress Reporting

| Macro | Default | Description |
|---|---|---|
| `FOTA_REPORT_PROGRESS` | `1` | POST progress events to the backend |
| `FOTA_PROGRESS_INTERVAL_PCT` | `10` | How often to report (every N %) |

### § 7 — Networking

| Macro | Default | Description |
|---|---|---|
| `FOTA_CONNECT_TIMEOUT_MS` | `15000` | TCP + TLS connect timeout in ms (BearSSL handshake is slow) |
| `FOTA_HTTP_TIMEOUT_MS` | `30000` | HTTP response timeout in ms |
| `FOTA_RETRY_COUNT` | `3` | Retry attempts on transient errors |
| `FOTA_RETRY_DELAY_MS` | `5000` | Delay between retries |

### § 8 — Behaviour

| Macro | Default | Description |
|---|---|---|
| `FOTA_REBOOT_ON_SUCCESS` | `0` | Automatically call `ESP.restart()` after successful flash |
| `FOTA_WATCHDOG_FEED` | `1` | Call `yield()` each chunk during download to feed the SWDT |

### § 9 — Logging

| Macro | Default | Description |
|---|---|---|
| `FOTA_LOG_LEVEL` | `3` | 0=off 1=error 2=warn 3=info 4=debug 5=verbose |
| `FOTA_LOG_TAG` | `"FOTA"` | Prefix printed before each log line |

Logs are written to `Serial` at the baud rate your sketch configures.

---

## API Reference

### FotaClient

```cpp
#include <FotaClient.h>
```

#### Constructor / Destructor

```cpp
FotaClient();
~FotaClient();   // frees BearSSL heap objects
```

#### Setup

| Method | Description |
|---|---|
| `void setServerUrl(const char *url)` | Override `FOTA_SERVER_URL` at runtime |
| `void setDeviceId(const char *id)` | Override `FOTA_DEVICE_ID` |
| `void setCurrentVersion(const char *ver)` | Override `FOTA_CURRENT_VERSION` |
| `void setApiKey(const char *key)` | Set static API key |
| `void setDeviceToken(const char *token)` | Set per-device bearer token |
| `void setCACert(const char *pem)` | Set root CA PEM (pass `nullptr` for insecure) |
| `void setClientCert(const char *pem)` | Set client certificate (mTLS) |
| `void setClientKey(const char *pem)` | Set client private key (mTLS) |
| `void setPublicKey(const char *pem)` | Embed public key for signature verification |
| `void onEvent(FotaEventCallback cb)` | Register lifecycle callback |

#### Operations

| Method | Returns | Description |
|---|---|---|
| `FotaResult begin()` | `FotaResult` | Validate config; must be called before `performUpdate()` |
| `FotaResult performUpdate()` | `FotaResult` | Run the full OTA pipeline |
| `FotaResult checkForUpdate(FotaManifest &m)` | `FotaResult` | Only fetch manifest — no download |
| `FotaResult fetchPublicKey()` | `FotaResult` | Fetch public key from server |

#### Status / Diagnostics

| Method | Returns | Description |
|---|---|---|
| `FotaState getState()` | `FotaState` | Current state enum |
| `FotaStats getStats()` | `FotaStats` | Download bytes, duration, retry count |
| `void resetStats()` | — | Reset stats counters |
| `uint32_t checkIntervalSecs()` | `uint32_t` | Server-recommended polling interval |
| `const char *getLastError()` | `const char *` | Human-readable last error string |

---

### FotaVerify

Low-level verification utilities (used internally, exposed for advanced users):

```cpp
#include <FotaVerify.h>
```

| Function | Description |
|---|---|
| `int fotaVerifySha256(const uint8_t *data, size_t len, const char *expectedHex)` | Verify SHA-256 digest |
| `int fotaVerifySha256Digest(mbedtls_md_context_t *ctx, const char *expectedHex)` | Finalise incremental SHA-256 |
| `int fotaVerifySignature(const uint8_t *digest, size_t dlen, const char *sigB64, const char *keyPem, const char *algorithm)` | Verify signature (`ECDSA_P256` or `RSA_SHA256` only; Ed25519 returns -1) |
| `int fotaBase64Decode(const char *b64, uint8_t *out, size_t *outLen)` | Base64 decode helper |

---

### Types and Enumerations

```cpp
enum FotaResult {
    FOTA_OK,
    FOTA_NO_UPDATE,
    FOTA_ERR_NOT_INITIALIZED,
    FOTA_ERR_WIFI_NOT_CONNECTED,
    FOTA_ERR_CONFIG,
    FOTA_ERR_HTTP,
    FOTA_ERR_PARSE,
    FOTA_ERR_DOWNLOAD,
    FOTA_ERR_VERIFY_FAILED,
    FOTA_ERR_FLASH,
    FOTA_ERR_SIZE,
    FOTA_ERR_OOM,
};

enum FotaState {
    FOTA_STATE_IDLE,
    FOTA_STATE_CHECKING,
    FOTA_STATE_DOWNLOADING,
    FOTA_STATE_VERIFYING,
    FOTA_STATE_INSTALLING,
    FOTA_STATE_COMPLETED,
    FOTA_STATE_ERROR,
};

enum FotaEvent {
    FOTA_EVENT_CHECK_START,
    FOTA_EVENT_UPDATE_AVAILABLE,
    FOTA_EVENT_NO_UPDATE,
    FOTA_EVENT_DOWNLOAD_START,
    FOTA_EVENT_DOWNLOAD_PROGRESS,
    FOTA_EVENT_DOWNLOAD_COMPLETE,
    FOTA_EVENT_VERIFY_START,
    FOTA_EVENT_VERIFY_COMPLETE,
    FOTA_EVENT_INSTALL_START,
    FOTA_EVENT_INSTALL_COMPLETE,
    FOTA_EVENT_ERROR,
    FOTA_EVENT_RETRY,
};

struct FotaManifest {
    char version[32];
    char url[256];
    char sha256[65];
    char signature[512];
    char signatureAlgorithm[32];
    char publicKeyUrl[256];
    uint32_t size;
    uint32_t checkIntervalSecs;
};

struct FotaPublicKey {
    char pem[1024];
    char algorithm[32];
};

struct FotaStats {
    uint32_t bytesDownloaded;
    uint32_t durationMs;
    uint8_t  retryCount;
};

typedef void (*FotaEventCallback)(FotaEvent event, const FotaClient *client);

const char *fotaResultStr(FotaResult r);
```

---

## Examples

### 1 — Minimal (development)

```cpp
#include <ESP8266WiFi.h>
#include <FotaClient.h>

FotaClient fota;

void setup() {
    Serial.begin(115200);
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    fota.setServerUrl("https://fota.example.com");
    fota.setDeviceId("esp8266-dev-001");
    fota.setCurrentVersion("1.0.0");
    // No CA cert — insecure, development only

    if (fota.performUpdate() == FOTA_OK) ESP.restart();
}
void loop() {}
```

### 2 — Production (CA cert + embedded public key)

```cpp
static const char CA_PEM[] PROGMEM   = R"(-----BEGIN CERTIFICATE-----...-----END CERTIFICATE-----)";
static const char PUB_PEM[] PROGMEM  = R"(-----BEGIN PUBLIC KEY-----...-----END PUBLIC KEY-----)";

#define FOTA_VERIFY_SIGNATURE 1
#include <FotaClient.h>

FotaClient fota;

void setup() {
    Serial.begin(115200);
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    fota.setServerUrl("https://fota.example.com");
    fota.setDeviceId("esp8266-prod-001");
    fota.setCurrentVersion("1.0.0");
    fota.setCACert(CA_PEM);
    fota.setPublicKey(PUB_PEM);

    FotaResult r = fota.performUpdate();
    Serial.printf("Result: %s\n", fotaResultStr(r));
    if (r == FOTA_OK) ESP.restart();
}
void loop() {}
```

### 3 — mTLS with client certificate

```cpp
#include <ESP8266WiFi.h>
#include <FotaClient.h>

static const char CA_PEM[]     PROGMEM = R"(...)";
static const char CLIENT_CRT[] PROGMEM = R"(...)";
static const char CLIENT_KEY[] PROGMEM = R"(...)";

FotaClient fota;

void setup() {
    Serial.begin(115200);
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    fota.setServerUrl("https://fota.example.com");
    fota.setDeviceId("esp8266-mtls-001");
    fota.setCurrentVersion("1.0.0");
    fota.setCACert(CA_PEM);
    fota.setClientCert(CLIENT_CRT);
    fota.setClientKey(CLIENT_KEY);

    if (fota.performUpdate() == FOTA_OK) ESP.restart();
}
void loop() {}
```

### 4 — Lifecycle callback (LED progress)

```cpp
#include <ESP8266WiFi.h>
#include <FotaClient.h>

FotaClient fota;

void onFota(FotaEvent ev, const FotaClient *c) {
    switch (ev) {
        case FOTA_EVENT_CHECK_START:        digitalWrite(LED_BUILTIN, LOW); break;
        case FOTA_EVENT_DOWNLOAD_PROGRESS:  Serial.printf("%.0f%%\n", 100.f * c->getStats().bytesDownloaded / 1024); break;
        case FOTA_EVENT_INSTALL_COMPLETE:   digitalWrite(LED_BUILTIN, HIGH); break;
        case FOTA_EVENT_ERROR:              Serial.printf("Error: %s\n", c->getLastError()); break;
        default: break;
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    fota.setServerUrl("https://fota.example.com");
    fota.setDeviceId("esp8266-001");
    fota.setCurrentVersion("1.0.0");
    fota.onEvent(onFota);

    if (fota.performUpdate() == FOTA_OK) ESP.restart();
}
void loop() {}
```

### 5 — Periodic OTA with server-supplied interval

```cpp
#include <ESP8266WiFi.h>
#include <FotaClient.h>

FotaClient fota;
uint32_t lastCheck = 0;
uint32_t intervalMs = 3600000; // 1 hour default

void setup() {
    Serial.begin(115200);
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    fota.setServerUrl("https://fota.example.com");
    fota.setDeviceId("esp8266-001");
    fota.setCurrentVersion("1.0.0");
    fota.setCACert(/* your CA PEM */nullptr);
}

void loop() {
    if (millis() - lastCheck >= intervalMs) {
        lastCheck = millis();
        FotaResult r = fota.performUpdate();
        if (r == FOTA_OK) {
            ESP.restart();
        }
        uint32_t srv = fota.checkIntervalSecs();
        if (srv > 0) intervalMs = srv * 1000UL;
    }
    // other tasks…
}
```

---

## PlatformIO Board Environments

The bundled `platformio.ini` provides three ready-to-use environments:

| Environment | Board |
|---|---|
| `esp8266_d1mini` (default) | `d1_mini` |
| `esp8266_nodemcu` | `nodemcuv2` |
| `esp8266_generic` | `esp8266_generic` |

Build for a specific environment:

```bash
pio run -e esp8266_nodemcu
```

---

## Security

1. **Always set a CA cert** — prevents MITM firmware injection.
2. **Enable signature verification** (`FOTA_VERIFY_SIGNATURE 1`) in production.
3. **Use mTLS** if your threat model requires device authentication at the TLS layer.
4. **Pin to a specific version range** (`^1.0.0`) in `lib_deps` — avoid floating `@latest`.
5. **Ed25519 is not supported** — use ECDSA P-256 or RSA-SHA256 for signing firmware.
6. **Verify free heap** before calling `performUpdate()` — BearSSL requires ~16 KB headroom.

---

## Troubleshooting

### `FOTA_ERR_HTTP` / connection timeout

- Ensure the backend is reachable from the device's network segment.
- Increase `FOTA_CONNECT_TIMEOUT_MS` (default 15 s) — BearSSL handshakes can be slow on
  congested networks.
- Check that the CA cert PEM is correct and the PROGMEM pointer is valid.

### `FOTA_ERR_VERIFY_FAILED` with algorithm log "ED25519 not supported"

- The server signed the firmware with Ed25519. Switch the platform signing key to ECDSA P-256
  or RSA-SHA256 in the FOTA backend settings.

### `FOTA_ERR_FLASH` / `Update.begin()` failed

- Not enough free space. With a 1 MiB flash chip you typically only have ~470 KB per OTA slot.
  Use a 4 MiB module (e.g. D1 Mini) and configure the partition layout accordingly.

### Heap crash / WDT reset during update

- Ensure at least 16 KB free heap before the OTA call.
- Enable `FOTA_WATCHDOG_FEED` (default on) so `yield()` is called every chunk.
- Reduce active allocations in your sketch before calling `performUpdate()`.

### `Serial` output garbled / no output

- Call `Serial.begin(115200)` (or your preferred baud rate) **before** `performUpdate()`.
- Set `FOTA_LOG_LEVEL 0` to disable logging if Serial is used for other protocols.

---

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for full version history.

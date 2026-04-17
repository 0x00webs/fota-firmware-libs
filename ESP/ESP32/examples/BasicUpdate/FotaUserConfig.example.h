// FotaUserConfig.h
// ----------------

#pragma once

// Wi‑Fi credentials
static const char* WIFI_SSID = "YOUR_WIFI_SSID";
static const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// FOTA backend configuration
#ifndef FOTA_SERVER_URL
#define FOTA_SERVER_URL "https://secure-iot-fota-platform.onrender.com"
#endif

#ifndef FOTA_CURRENT_VERSION
#define FOTA_CURRENT_VERSION "1.0.0"
#endif

#define FOTA_DEVICE_ID "YOUR_DEVICE_ID"
#define FOTA_HARDWARE_MODEL "YOUR_HARDWARE_MODEL"

// Signature verification (recommended in production)
#define FOTA_VERIFY_SIGNATURE 1
#define FOTA_AUTO_FETCH_PUBLIC_KEY 1

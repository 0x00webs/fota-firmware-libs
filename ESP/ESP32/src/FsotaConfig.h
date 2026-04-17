#pragma once

/*
 * FsotaConfig.h — Compile-time configuration for the NodeWave FOTA ESP32 filesystem OTA client
 *
 * v1.0.0
 *
 * All FSOTA_* defaults are defined at the bottom of FotaConfig.h (§ FSOTA section).
 * This file exists so that FsotaClient.h can include "FsotaConfig.h" without
 * needing to know whether it is included separately or merged into FotaConfig.h.
 *
 * Overrides should be placed in FotaUserConfig.h alongside your sketch — they
 * will be picked up automatically (FotaConfig.h includes FotaUserConfig.h when
 * it is present on the compiler include path).
 */

// Pull in FotaConfig.h which defines both FOTA_* and FSOTA_* symbols.
#include "FotaConfig.h"

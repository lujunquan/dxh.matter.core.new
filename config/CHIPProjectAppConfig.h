/*
 *    Copyright (c) 2026 DUWI Matter Bridge Authors
 *    SPDX-License-Identifier: Apache-2.0
 */

#pragma once

// Number of dynamic bridged endpoints that can exist at the same time.
// Keep in sync with DYNAMIC_ENDPOINT_COUNT in zap/BUILD.gn.
#define CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT 16

// Current values are the CSA test vendor/product IDs. They are fine for
// development and certification dry-runs only. Replace them with the IDs
// assigned to DUWI before production or certification.
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID 0xFFF1
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME "DUWI"
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID 0x8001
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "DUWI Matter Bridge"

// Pull in the standalone defaults (test setup parameters, etc.).
#include <CHIPProjectConfig.h>
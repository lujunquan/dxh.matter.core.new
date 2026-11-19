/*
 *    Copyright (c) 2026 DUWI Matter Bridge Authors
 *    SPDX-License-Identifier: Apache-2.0
 *
 * SDK-independent domain model shared by adapters and the bridge layer.
 * No connectedhomeip headers may be included here.
 */

#pragma once

#include <cstdint>
#include <string>

namespace dxh::model {

/// Device categories that the bridge currently maps to Matter device types.
enum class DeviceKind : uint8_t
{
    kOnOffLight        = 1,
    kTemperatureSensor = 2,
};

/// Result type of an adapter command. Kept separate from CHIP_ERROR so that
/// adapters can be built and tested without the Matter SDK.
enum class AdapterStatus : uint8_t
{
    kOk = 0,
    kNotSupported,
    kInvalidArgument,
    kNotFound,
    kTransportError,
};

/// State that can be reported by a physical device.
struct DeviceState
{
    bool reachable = false;
    bool onOff     = false;

    // Temperature is expressed in 0.01 degrees Celsius, matching the Matter
    // TemperatureMeasurement cluster.
    int16_t measuredValue    = 0;
    int16_t minMeasuredValue = 0;
    int16_t maxMeasuredValue = 0;
};

/// Snapshot of one physical device as seen by an adapter.
struct DeviceDescriptor
{
    /// Stable identifier inside the adapter's ecosystem. Must survive reboots,
    /// firmware upgrades and re-pairing.
    std::string physicalId;

    /// User-facing name, used as the Bridged Device Basic Information node label.
    std::string name;

    /// Room or zone name. Reserved for the future Actions/room mapping.
    std::string location;

    DeviceKind kind = DeviceKind::kOnOffLight;
    DeviceState state;
};

inline const char * DeviceKindName(DeviceKind kind)
{
    switch (kind)
    {
    case DeviceKind::kOnOffLight:
        return "on-off-light";
    case DeviceKind::kTemperatureSensor:
        return "temperature-sensor";
    }
    return "unknown";
}

} // namespace dxh::model

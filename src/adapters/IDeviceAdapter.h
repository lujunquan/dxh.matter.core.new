/*
 *    Copyright (c) 2026 DUWI Matter Bridge Authors
 *    SPDX-License-Identifier: Apache-2.0
 *
 * Transport-independent interface between the Matter bridge and the DUWI
 * device ecosystem. The concrete transport (MQTT, HTTP, BLE, proprietary
 * gateway protocol, ...) is intentionally not decided yet: it will be added
 * as one or more implementations of this interface.
 *
 * This header must stay free of connectedhomeip includes.
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "model/DeviceTypes.h"

namespace dxh::adapters {

class IDeviceAdapter
{
public:
    /// Called when a physical device reports a state change. The callback may
    /// be invoked from any thread; the bridge schedules the update onto the
    /// Matter thread.
    using StateChangedCallback = std::function<void(const model::DeviceDescriptor &)>;

    virtual ~IDeviceAdapter() = default;

    /// Stable adapter identity, e.g. "duwi". It participates in the derived
    /// Matter UniqueID and must never change for a deployed product line.
    virtual const char * Name() const = 0;

    /// Connect to the device ecosystem. Called once on startup before
    /// Discover(). Returning false aborts bridge startup.
    virtual bool Start() { return true; }

    /// Disconnect from the device ecosystem. Called on shutdown.
    virtual void Stop() {}

    /// Enumerate currently known devices with their initial state.
    virtual std::vector<model::DeviceDescriptor> Discover() = 0;

    /// Command sent by a Matter controller.
    virtual model::AdapterStatus SetOnOff(const std::string & physicalId, bool on) = 0;

    /// Register the state-change callback. The bridge calls this before Start().
    virtual void SetStateChangedCallback(StateChangedCallback callback) = 0;
};

} // namespace dxh::adapters

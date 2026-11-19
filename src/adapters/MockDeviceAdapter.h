/*
 *    Copyright (c) 2026 DUWI Matter Bridge Authors
 *    SPDX-License-Identifier: Apache-2.0
 *
 * Deterministic in-memory adapter used until a real DUWI transport adapter is
 * available. It also provides the console simulation hooks used for manual
 * testing.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "IDeviceAdapter.h"

namespace dxh::adapters {

class MockDeviceAdapter final : public IDeviceAdapter
{
public:
    MockDeviceAdapter();

    const char * Name() const override { return "duwi"; }
    bool Start() override { return true; }
    void Stop() override {}

    std::vector<model::DeviceDescriptor> Discover() override;
    model::AdapterStatus SetOnOff(const std::string & physicalId, bool on) override;
    void SetStateChangedCallback(StateChangedCallback callback) override { mCallback = std::move(callback); }

    // Simulation hooks. They model events that a real adapter will receive
    // from the DUWI cloud/gateway. Must be called on the Matter thread.
    void SimulateToggleAll();
    void SimulateTemperatureDelta(int16_t delta);
    void SimulateFirstTemperatureSensorReachability(bool reachable);

private:
    model::DeviceDescriptor * Find(const std::string & physicalId);
    void Notify(const model::DeviceDescriptor & descriptor);

    std::vector<model::DeviceDescriptor> mDevices;
    StateChangedCallback mCallback;
};

} // namespace dxh::adapters

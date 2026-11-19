/*
 *    Copyright (c) 2026 DUWI Matter Bridge Authors
 *    SPDX-License-Identifier: Apache-2.0
 *
 * Composition root: wires the device adapter to the Matter bridge layer and
 * owns the application lifecycle.
 */

#pragma once

#include <protocols/interaction_model/StatusCode.h>

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <pthread.h>

#include "adapters/MockDeviceAdapter.h"
#include "bridge/BridgedDevice.h"
#include "model/DeviceTypes.h"

namespace dxh::app {

class BridgeApplication
{
public:
    static BridgeApplication & Instance();

    /// Called from ApplicationInit() after the Matter stack is initialized.
    void Init();

    /// Called from ApplicationShutdown() before core Matter objects are torn
    /// down.
    void Shutdown();

private:
    BridgeApplication() = default;

    void AddDiscoveredDevices();
    std::unique_ptr<bridge::BridgedDevice> CreateDevice(const model::DeviceDescriptor & descriptor);

    void OnAdapterStateChanged(const model::DeviceDescriptor & descriptor);
    static void ApplyAdapterUpdate(intptr_t context);

    static chip::Protocols::InteractionModel::Status HandleMatterOnOffWrite(bridge::BridgedOnOffDevice & device, bool on);

    void StartConsoleThread();
    void StopConsoleThread();
    void ConsoleLoop();
    void HandleConsoleInput(int ch);

    std::unique_ptr<adapters::MockDeviceAdapter> mAdapter;
    std::vector<std::unique_ptr<bridge::BridgedDevice>> mDevices;
    std::unordered_map<std::string, bridge::BridgedDevice *> mDevicesByPhysicalId;

    pthread_t mConsoleThread{};
    bool mConsoleThreadStarted = false;
    std::atomic<bool> mConsoleRunning{ false };
};

} // namespace dxh::app

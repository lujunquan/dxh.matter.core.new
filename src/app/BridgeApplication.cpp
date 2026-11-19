/*
 *    Copyright (c) 2026 DUWI Matter Bridge Authors
 *    SPDX-License-Identifier: Apache-2.0
 */

#include "BridgeApplication.h"

#include <AppMain.h>

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/reporting/reporting.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>

#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdio>
#include <utility>

#include "bridge/BridgeEndpointManager.h"
#include "bridge/BridgedAttributeAccess.h"
#include "bridge/StableId.h"

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using chip::Protocols::InteractionModel::Status;

namespace dxh::app {

namespace {

constexpr int kConsolePollMs = 100;

bool ConsoleHasPendingInput()
{
    int bytesWaiting = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    if (ioctl(STDIN_FILENO, FIONREAD, &bytesWaiting) != 0)
    {
        return false;
    }
    return bytesWaiting > 0;
}

} // namespace

BridgeApplication & BridgeApplication::Instance()
{
    static BridgeApplication instance;
    return instance;
}

void BridgeApplication::Init()
{
    ChipLogProgress(AppServer, "DUWI Matter bridge initializing");

    bridge::BridgeEndpointManager::Instance().Initialize();
    bridge::SetOnOffWriteHandler(&BridgeApplication::HandleMatterOnOffWrite);

    mAdapter = std::make_unique<adapters::MockDeviceAdapter>();
    mAdapter->SetStateChangedCallback(
        [](const model::DeviceDescriptor & descriptor) { BridgeApplication::Instance().OnAdapterStateChanged(descriptor); });

    if (!mAdapter->Start())
    {
        ChipLogError(AppServer, "Device adapter \"%s\" failed to start", mAdapter->Name());
        return;
    }

    AddDiscoveredDevices();
    StartConsoleThread();

    ChipLogProgress(AppServer, "DUWI Matter bridge ready. Console: c=toggle lights, t=+1C, u=sensor offline, v=sensor online");
}

void BridgeApplication::Shutdown()
{
    ChipLogProgress(AppServer, "DUWI Matter bridge shutting down");

    StopConsoleThread();
    bridge::SetOnOffWriteHandler(nullptr);

    if (mAdapter != nullptr)
    {
        mAdapter->Stop();
    }

    for (auto it = mDevices.rbegin(); it != mDevices.rend(); ++it)
    {
        bridge::BridgeEndpointManager::Instance().RemoveDevice(it->get());
    }
    mDevices.clear();
    mDevicesByPhysicalId.clear();
    mAdapter.reset();
}

void BridgeApplication::AddDiscoveredDevices()
{
    const std::vector<model::DeviceDescriptor> descriptors = mAdapter->Discover();
    ChipLogProgress(AppServer, "Adapter \"%s\" discovered %zu device(s)", mAdapter->Name(), descriptors.size());

    for (const auto & descriptor : descriptors)
    {
        auto device    = CreateDevice(descriptor);
        const int slot = bridge::BridgeEndpointManager::Instance().AddDevice(device.get());
        if (slot < 0)
        {
            ChipLogError(AppServer, "Skipping device \"%s\": could not allocate a dynamic endpoint", descriptor.physicalId.c_str());
            continue;
        }

        mDevicesByPhysicalId.emplace(device->GetPhysicalId(), device.get());
        mDevices.push_back(std::move(device));
    }
}

std::unique_ptr<bridge::BridgedDevice> BridgeApplication::CreateDevice(const model::DeviceDescriptor & descriptor)
{
    const std::string uniqueId = bridge::MakeStableUniqueId(mAdapter->Name(), descriptor.physicalId);

    if (descriptor.kind == model::DeviceKind::kTemperatureSensor)
    {
        auto device = std::make_unique<bridge::BridgedTemperatureSensor>(descriptor, uniqueId);
        device->SetChangedCallback([](bridge::BridgedTemperatureSensor & sensor) {
            MatterReportingAttributeChangeCallback(sensor.GetEndpointId(), TemperatureMeasurement::Id,
                                                   TemperatureMeasurement::Attributes::MeasuredValue::Id);
        });
        return device;
    }

    auto device = std::make_unique<bridge::BridgedOnOffDevice>(descriptor, uniqueId);
    device->SetChangedCallback([](bridge::BridgedOnOffDevice & light) {
        MatterReportingAttributeChangeCallback(light.GetEndpointId(), OnOff::Id, OnOff::Attributes::OnOff::Id);
    });
    return device;
}

void BridgeApplication::OnAdapterStateChanged(const model::DeviceDescriptor & descriptor)
{
    auto * copy = Platform::New<model::DeviceDescriptor>(descriptor);
    VerifyOrReturn(copy != nullptr, ChipLogError(AppServer, "Out of memory while applying adapter update"));

    const CHIP_ERROR err =
        DeviceLayer::PlatformMgr().ScheduleWork(&BridgeApplication::ApplyAdapterUpdate, reinterpret_cast<intptr_t>(copy));
    if (err != CHIP_NO_ERROR)
    {
        Platform::Delete(copy);
        LogErrorOnFailure(err);
    }
}

void BridgeApplication::ApplyAdapterUpdate(intptr_t context)
{
    auto * descriptor = reinterpret_cast<model::DeviceDescriptor *>(context);
    auto & app        = BridgeApplication::Instance();

    const auto it = app.mDevicesByPhysicalId.find(descriptor->physicalId);
    if (it == app.mDevicesByPhysicalId.end())
    {
        ChipLogError(AppServer, "Adapter update for unknown device \"%s\"", descriptor->physicalId.c_str());
        Platform::Delete(descriptor);
        return;
    }

    bridge::BridgedDevice * device = it->second;
    device->SetReachable(descriptor->state.reachable);

    switch (device->GetKind())
    {
    case model::DeviceKind::kOnOffLight:
        static_cast<bridge::BridgedOnOffDevice *>(device)->ApplyOnOff(descriptor->state.onOff);
        break;
    case model::DeviceKind::kTemperatureSensor:
        static_cast<bridge::BridgedTemperatureSensor *>(device)->ApplyMeasuredValue(descriptor->state.measuredValue);
        break;
    }

    Platform::Delete(descriptor);
}

Status BridgeApplication::HandleMatterOnOffWrite(bridge::BridgedOnOffDevice & device, bool on)
{
    auto & app = BridgeApplication::Instance();
    if (app.mAdapter == nullptr)
    {
        return Status::Failure;
    }

    const model::AdapterStatus result = app.mAdapter->SetOnOff(device.GetPhysicalId(), on);
    if (result != model::AdapterStatus::kOk)
    {
        ChipLogError(AppServer, "Adapter rejected OnOff=%d for \"%s\" (status %u)", on ? 1 : 0, device.GetPhysicalId().c_str(),
                     static_cast<unsigned>(result));
        return Status::Failure;
    }
    return Status::Success;
}

void BridgeApplication::StartConsoleThread()
{
    mConsoleRunning.store(true);
    const int result = pthread_create(
        &mConsoleThread, nullptr,
        [](void *) -> void * {
            BridgeApplication::Instance().ConsoleLoop();
            return nullptr;
        },
        nullptr);

    if (result != 0)
    {
        mConsoleRunning.store(false);
        ChipLogError(AppServer, "Failed to start console thread (%d); console simulation disabled", result);
        return;
    }
    mConsoleThreadStarted = true;
}

void BridgeApplication::StopConsoleThread()
{
    mConsoleRunning.store(false);
    if (!mConsoleThreadStarted)
    {
        return;
    }
    pthread_join(mConsoleThread, nullptr);
    mConsoleThreadStarted = false;
}

void BridgeApplication::ConsoleLoop()
{
    while (mConsoleRunning.load())
    {
        if (!ConsoleHasPendingInput())
        {
            usleep(static_cast<useconds_t>(kConsolePollMs) * 1000);
            continue;
        }

        const int ch = getchar();
        if (ch == EOF)
        {
            break;
        }

        // Device state must be touched on the Matter thread.
        DeviceLayer::SystemLayer().ScheduleLambda([ch]() { BridgeApplication::Instance().HandleConsoleInput(ch); });
    }
}

void BridgeApplication::HandleConsoleInput(int ch)
{
    if (mAdapter == nullptr)
    {
        return;
    }

    switch (ch)
    {
    case 'c':
        mAdapter->SimulateToggleAll();
        break;
    case 't':
        mAdapter->SimulateTemperatureDelta(100);
        break;
    case 'u':
        mAdapter->SimulateFirstTemperatureSensorReachability(false);
        break;
    case 'v':
        mAdapter->SimulateFirstTemperatureSensorReachability(true);
        break;
    default:
        ChipLogProgress(AppServer, "Unknown console command '%c'", static_cast<char>(ch));
        break;
    }
}

} // namespace dxh::app

// ---------------------------------------------------------------------------
// Hooks required by examples/platform/linux:app-main.

void ApplicationInit()
{
    dxh::app::BridgeApplication::Instance().Init();
}

void ApplicationShutdown()
{
    dxh::app::BridgeApplication::Instance().Shutdown();
}

/*
 *    Copyright (c) 2026 DUWI Matter Bridge Authors
 *    SPDX-License-Identifier: Apache-2.0
 *
 * Host-only tests for the SDK-independent core (model, adapters, stable id).
 * No connectedhomeip headers, gn/ninja or CIPD environment required.
 */

#include <cstdio>
#include <string>
#include <vector>

#include "adapters/MockDeviceAdapter.h"
#include "bridge/StableId.h"

namespace {

int gChecks   = 0;
int gFailures = 0;

void Check(bool condition, const char * expression, const char * file, int line)
{
    ++gChecks;
    if (!condition)
    {
        ++gFailures;
        std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
    }
}

#define CHECK(expr) Check((expr), #expr, __FILE__, __LINE__)

bool IsUpperHex(const std::string & value)
{
    for (const char c : value)
    {
        const bool digit = c >= '0' && c <= '9';
        const bool upper = c >= 'A' && c <= 'F';
        if (!digit && !upper)
        {
            return false;
        }
    }
    return true;
}

const dxh::model::DeviceDescriptor * FindDevice(const std::vector<dxh::model::DeviceDescriptor> & devices,
                                                const std::string & physicalId)
{
    for (const auto & device : devices)
    {
        if (device.physicalId == physicalId)
        {
            return &device;
        }
    }
    return nullptr;
}

void TestStableId()
{
    const std::string id = dxh::bridge::MakeStableUniqueId("duwi", "duwi-lamp-001");

    CHECK(id.size() == 32);
    CHECK(IsUpperHex(id));
    CHECK(id == dxh::bridge::MakeStableUniqueId("duwi", "duwi-lamp-001"));
    CHECK(id != dxh::bridge::MakeStableUniqueId("duwi", "duwi-lamp-002"));
    CHECK(id != dxh::bridge::MakeStableUniqueId("other-adapter", "duwi-lamp-001"));
}

void TestMockAdapterDiscovery()
{
    dxh::adapters::MockDeviceAdapter adapter;
    adapter.SetStateChangedCallback(nullptr);

    const auto devices = adapter.Discover();
    CHECK(devices.size() == 3);

    const auto * lamp = FindDevice(devices, "duwi-lamp-001");
    CHECK(lamp != nullptr);
    if (lamp != nullptr)
    {
        CHECK(lamp->kind == dxh::model::DeviceKind::kOnOffLight);
        CHECK(lamp->state.reachable);
    }

    const auto * sensor = FindDevice(devices, "duwi-sensor-001");
    CHECK(sensor != nullptr);
    if (sensor != nullptr)
    {
        CHECK(sensor->kind == dxh::model::DeviceKind::kTemperatureSensor);
        CHECK(sensor->state.minMeasuredValue < sensor->state.maxMeasuredValue);
        CHECK(sensor->state.measuredValue >= sensor->state.minMeasuredValue);
        CHECK(sensor->state.measuredValue <= sensor->state.maxMeasuredValue);
    }
}

void TestMockAdapterCommands()
{
    dxh::adapters::MockDeviceAdapter adapter;
    int notifications = 0;
    dxh::model::DeviceDescriptor lastNotification;
    adapter.SetStateChangedCallback([&](const dxh::model::DeviceDescriptor & descriptor) {
        ++notifications;
        lastNotification = descriptor;
    });

    CHECK(adapter.SetOnOff("duwi-lamp-001", true) == dxh::model::AdapterStatus::kOk);
    CHECK(notifications == 1);
    CHECK(lastNotification.physicalId == "duwi-lamp-001");
    CHECK(lastNotification.state.onOff);

    CHECK(adapter.SetOnOff("does-not-exist", true) == dxh::model::AdapterStatus::kNotFound);
    CHECK(adapter.SetOnOff("duwi-sensor-001", true) == dxh::model::AdapterStatus::kNotSupported);

    adapter.SimulateFirstTemperatureSensorReachability(false);
    CHECK(adapter.SetOnOff("duwi-lamp-001", false) == dxh::model::AdapterStatus::kOk);

    const auto devices = adapter.Discover();
    const auto * lamp  = FindDevice(devices, "duwi-lamp-001");
    CHECK(lamp != nullptr);
    if (lamp != nullptr)
    {
        CHECK(!lamp->state.onOff);
    }

    const auto * sensor = FindDevice(devices, "duwi-sensor-001");
    CHECK(sensor != nullptr);
    if (sensor != nullptr)
    {
        CHECK(!sensor->state.reachable);
    }
}

void TestMockAdapterSimulation()
{
    dxh::adapters::MockDeviceAdapter adapter;

    const auto before         = adapter.Discover();
    const auto * sensorBefore = FindDevice(before, "duwi-sensor-001");
    CHECK(sensorBefore != nullptr);

    adapter.SimulateToggleAll();
    adapter.SimulateTemperatureDelta(100 + 1);

    const auto after         = adapter.Discover();
    const auto * lamp        = FindDevice(after, "duwi-lamp-001");
    const auto * sensorAfter = FindDevice(after, "duwi-sensor-001");
    CHECK(lamp != nullptr);
    CHECK(sensorAfter != nullptr);

    if (sensorBefore != nullptr && sensorAfter != nullptr)
    {
        CHECK(sensorAfter->state.measuredValue != sensorBefore->state.measuredValue);
        CHECK(sensorAfter->state.measuredValue <= sensorAfter->state.maxMeasuredValue);
        CHECK(sensorAfter->state.measuredValue >= sensorAfter->state.minMeasuredValue);
    }
}

} // namespace

int main()
{
    TestStableId();
    TestMockAdapterDiscovery();
    TestMockAdapterCommands();
    TestMockAdapterSimulation();

    std::printf("%d checks, %d failure(s)\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

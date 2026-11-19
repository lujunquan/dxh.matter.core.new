/*
 *    Copyright (c) 2026 DUWI Matter Bridge Authors
 *    SPDX-License-Identifier: Apache-2.0
 */

#include "MockDeviceAdapter.h"

#include <algorithm>

namespace dxh::adapters {

namespace {

constexpr int16_t kMinTemperature = -4000; // -40.00 C
constexpr int16_t kMaxTemperature = 12500; // 125.00 C

model::DeviceDescriptor MakeOnOffLight(const char * physicalId, const char * name, const char * location, bool on)
{
    model::DeviceDescriptor descriptor;
    descriptor.physicalId      = physicalId;
    descriptor.name            = name;
    descriptor.location        = location;
    descriptor.kind            = model::DeviceKind::kOnOffLight;
    descriptor.state.reachable = true;
    descriptor.state.onOff     = on;
    return descriptor;
}

model::DeviceDescriptor MakeTemperatureSensor(const char * physicalId, const char * name, const char * location,
                                              int16_t measuredValue)
{
    model::DeviceDescriptor descriptor;
    descriptor.physicalId             = physicalId;
    descriptor.name                   = name;
    descriptor.location               = location;
    descriptor.kind                   = model::DeviceKind::kTemperatureSensor;
    descriptor.state.reachable        = true;
    descriptor.state.measuredValue    = measuredValue;
    descriptor.state.minMeasuredValue = kMinTemperature;
    descriptor.state.maxMeasuredValue = kMaxTemperature;
    return descriptor;
}

} // namespace

MockDeviceAdapter::MockDeviceAdapter()
{
    mDevices.push_back(MakeOnOffLight("duwi-lamp-001", "DUWI Lamp 1", "Living Room", false));
    mDevices.push_back(MakeOnOffLight("duwi-lamp-002", "DUWI Lamp 2", "Bedroom", true));
    mDevices.push_back(MakeTemperatureSensor("duwi-sensor-001", "DUWI Temperature 1", "Living Room", 2150));
}

std::vector<model::DeviceDescriptor> MockDeviceAdapter::Discover()
{
    return mDevices;
}

model::AdapterStatus MockDeviceAdapter::SetOnOff(const std::string & physicalId, bool on)
{
    auto * device = Find(physicalId);
    if (device == nullptr)
    {
        return model::AdapterStatus::kNotFound;
    }
    if (device->kind != model::DeviceKind::kOnOffLight)
    {
        return model::AdapterStatus::kNotSupported;
    }
    if (!device->state.reachable)
    {
        return model::AdapterStatus::kTransportError;
    }

    device->state.onOff = on;
    Notify(*device);
    return model::AdapterStatus::kOk;
}

void MockDeviceAdapter::SimulateToggleAll()
{
    for (auto & device : mDevices)
    {
        if (device.kind != model::DeviceKind::kOnOffLight)
        {
            continue;
        }
        device.state.onOff = !device.state.onOff;
        Notify(device);
    }
}

void MockDeviceAdapter::SimulateTemperatureDelta(int16_t delta)
{
    for (auto & device : mDevices)
    {
        if (device.kind != model::DeviceKind::kTemperatureSensor)
        {
            continue;
        }
        const int32_t updated      = static_cast<int32_t>(device.state.measuredValue) + delta;
        device.state.measuredValue = static_cast<int16_t>(std::clamp(updated, static_cast<int32_t>(device.state.minMeasuredValue),
                                                                     static_cast<int32_t>(device.state.maxMeasuredValue)));
        Notify(device);
    }
}

void MockDeviceAdapter::SimulateFirstTemperatureSensorReachability(bool reachable)
{
    for (auto & device : mDevices)
    {
        if (device.kind != model::DeviceKind::kTemperatureSensor)
        {
            continue;
        }
        device.state.reachable = reachable;
        Notify(device);
        return;
    }
}

model::DeviceDescriptor * MockDeviceAdapter::Find(const std::string & physicalId)
{
    for (auto & device : mDevices)
    {
        if (device.physicalId == physicalId)
        {
            return &device;
        }
    }
    return nullptr;
}

void MockDeviceAdapter::Notify(const model::DeviceDescriptor & descriptor)
{
    if (mCallback)
    {
        mCallback(descriptor);
    }
}

} // namespace dxh::adapters

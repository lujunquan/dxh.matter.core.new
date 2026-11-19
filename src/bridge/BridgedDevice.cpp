/*
 *    Copyright (c) 2026 DUWI Matter Bridge Authors
 *    SPDX-License-Identifier: Apache-2.0
 *
 * Portions of this file are derived from the Matter SDK bridge example
 * (examples/bridge-app/linux/Device.cpp), Copyright (c) 2021-2022 Project
 * CHIP Authors, licensed under Apache-2.0.
 */

#include "BridgedDevice.h"

#include <app/clusters/basic-information/CodegenIntegration.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <lib/support/CHIPMemString.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/DefaultTimerDelegate.h>

#include <algorithm>
#include <utility>

using namespace chip;
using chip::Protocols::InteractionModel::Status;

namespace dxh::bridge {

namespace {

// Forwards ConfigurationVersion increases to the device's Basic Information
// cluster, as required for bridged devices that use codegen data model
// clusters.
class BridgeConfigurationVersionDelegate : public chip::app::Clusters::ConfigurationVersionDelegate
{
public:
    CHIP_ERROR IncreaseConfigurationVersion() override
    {
        auto * cluster = app::Clusters::BasicInformation::GetClusterInstance();
        VerifyOrReturnError(cluster != nullptr, CHIP_ERROR_NOT_FOUND);
        return cluster->IncreaseConfigurationVersion();
    }
};

BridgeConfigurationVersionDelegate gConfigurationVersionDelegate;

} // namespace

BridgedDevice::BridgedDevice(const model::DeviceDescriptor & descriptor, std::string uniqueId) :
    mPhysicalId(descriptor.physicalId), mLocation(descriptor.location), mKind(descriptor.kind),
    mInitialReachable(descriptor.state.reachable)
{
    Platform::CopyString(mName, descriptor.name.c_str());
    if (uniqueId.size() > kUniqueIdSize)
    {
        uniqueId.resize(kUniqueIdSize);
    }
    Platform::CopyString(mUniqueId, uniqueId.c_str());
}

void BridgedDevice::Unregister()
{
    if (mBridgedDevice.IsConstructed())
    {
        LogErrorOnFailure(chip::app::CodegenDataModelProvider::Instance().Registry().Unregister(&mBridgedDevice.Cluster()));
        mBridgedDevice.Destroy();
    }
}

Status BridgedDevice::OnNodeLabelChanged(const std::string & newNodeLabel)
{
    if (mName == newNodeLabel)
    {
        return Status::Success;
    }
    Platform::CopyString(mName, newNodeLabel.c_str());
    // The Bridged Device Basic Information cluster reports its own attribute
    // changes, so no explicit reporting callback is needed here.
    return Status::Success;
}

chip::app::ServerClusterRegistration &
BridgedDevice::CreateBridgedDeviceInfo(chip::EndpointId endpointId,
                                       chip::app::Clusters::BridgedDeviceBasicInformationCluster::MutableData && mutableData,
                                       chip::app::Clusters::BridgedDeviceBasicInformationCluster::FixedData && fixedData)
{
    VerifyOrDie(!mBridgedDevice.IsConstructed());

    static chip::app::DefaultTimerDelegate timerDelegate;

    const uint32_t version = mutableData.configurationVersion.has_value() ? mutableData.configurationVersion->version : 1;
    mutableData.configurationVersion.emplace(chip::app::Clusters::BridgedDeviceBasicInformationCluster::Versioning{
        .version = version, .delegate = gConfigurationVersionDelegate });

    mBridgedDevice.Create(
        endpointId, std::move(mutableData), std::move(fixedData),
        chip::app::Clusters::BridgedDeviceBasicInformationCluster::Context{ .delegate = *this, .timerDelegate = timerDelegate });
    return mBridgedDevice.Registration();
}

bool BridgedDevice::IsReachable()
{
    return mBridgedDevice.IsConstructed() && mBridgedDevice.Cluster().GetReachable();
}

void BridgedDevice::SetReachable(bool reachable)
{
    VerifyOrReturn(mBridgedDevice.IsConstructed());
    VerifyOrReturn(mBridgedDevice.Cluster().GetReachable() != reachable);

    mBridgedDevice.Cluster().SetReachable(reachable);
    ChipLogProgress(DeviceLayer, "BridgedDevice[%s]: %s", mName, reachable ? "ONLINE" : "OFFLINE");
}

void BridgedDevice::SetName(const char * name)
{
    if (name == nullptr)
    {
        return;
    }
    Platform::CopyString(mName, name);
    if (mBridgedDevice.IsConstructed())
    {
        mBridgedDevice.Cluster().SetNodeLabel(CharSpan::fromCharString(mName));
    }
}

void BridgedDevice::SetLocation(std::string location)
{
    if (mLocation == location)
    {
        return;
    }
    mLocation = std::move(location);
    ChipLogProgress(DeviceLayer, "BridgedDevice[%s]: location=\"%s\"", mName, mLocation.c_str());
}

uint32_t BridgedDevice::GetConfigurationVersion()
{
    VerifyOrReturnValue(mBridgedDevice.IsConstructed(), 1);
    return mBridgedDevice.Cluster().GetConfigurationVersion().value_or(1);
}

void BridgedDevice::IncreaseConfigurationVersion()
{
    VerifyOrReturn(mBridgedDevice.IsConstructed());
    LogErrorOnFailure(mBridgedDevice.Cluster().IncreaseConfigurationVersion());
    ChipLogProgress(DeviceLayer, "BridgedDevice[%s]: configuration version=%u", mName, GetConfigurationVersion());
}

BridgedOnOffDevice::BridgedOnOffDevice(const model::DeviceDescriptor & descriptor, std::string uniqueId) :
    BridgedDevice(descriptor, std::move(uniqueId)), mOn(descriptor.state.onOff)
{}

void BridgedOnOffDevice::ApplyOnOff(bool on)
{
    if (mOn == on)
    {
        return;
    }
    mOn = on;
    ChipLogProgress(DeviceLayer, "BridgedDevice[%s]: %s", mName, on ? "ON" : "OFF");

    if (mChangedCallback)
    {
        mChangedCallback(*this);
    }
}

BridgedTemperatureSensor::BridgedTemperatureSensor(const model::DeviceDescriptor & descriptor, std::string uniqueId) :
    BridgedDevice(descriptor, std::move(uniqueId)), mMinMeasuredValue(descriptor.state.minMeasuredValue),
    mMaxMeasuredValue(descriptor.state.maxMeasuredValue),
    mMeasuredValue(std::clamp(descriptor.state.measuredValue, descriptor.state.minMeasuredValue, descriptor.state.maxMeasuredValue))
{}

void BridgedTemperatureSensor::ApplyMeasuredValue(int16_t measuredValue)
{
    const int16_t clamped = std::clamp(measuredValue, mMinMeasuredValue, mMaxMeasuredValue);
    if (clamped == mMeasuredValue)
    {
        return;
    }
    mMeasuredValue = clamped;
    ChipLogProgress(DeviceLayer, "BridgedDevice[%s]: measured value=%d", mName, mMeasuredValue);

    if (mChangedCallback)
    {
        mChangedCallback(*this);
    }
}

} // namespace dxh::bridge

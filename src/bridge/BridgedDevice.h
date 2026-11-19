/*
 *    Copyright (c) 2026 DUWI Matter Bridge Authors
 *    SPDX-License-Identifier: Apache-2.0
 *
 * Matter-side representation of a bridged physical device. These classes own
 * the Bridged Device Basic Information cluster and the mirrored state that is
 * exposed through dynamic endpoints.
 */

#pragma once

#include <app/clusters/bridged-device-basic-information-server/BridgedDeviceBasicInformationCluster.h>
#include <app/server-cluster/ServerClusterInterfaceRegistry.h>
#include <app/util/attribute-storage.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "model/DeviceTypes.h"

namespace dxh::bridge {

class BridgedDevice : public chip::app::Clusters::BridgedDeviceBasicInformationDelegate
{
public:
    static constexpr size_t kNameSize     = 32;
    static constexpr size_t kUniqueIdSize = 32;

    BridgedDevice(const model::DeviceDescriptor & descriptor, std::string uniqueId);
    ~BridgedDevice() override = default;

    // Endpoint bookkeeping, owned by BridgeEndpointManager.
    void SetEndpointId(chip::EndpointId endpointId) { mEndpointId = endpointId; }
    chip::EndpointId GetEndpointId() const { return mEndpointId; }
    void SetParentEndpointId(chip::EndpointId endpointId) { mParentEndpointId = endpointId; }
    chip::EndpointId GetParentEndpointId() const { return mParentEndpointId; }

    const std::string & GetPhysicalId() const { return mPhysicalId; }
    model::DeviceKind GetKind() const { return mKind; }
    bool GetInitialReachable() const { return mInitialReachable; }

    bool IsReachable();
    void SetReachable(bool reachable);

    void SetName(const char * name);
    const char * GetName() const { return mName; }
    const char * GetUniqueId() const { return mUniqueId; }

    const std::string & GetLocation() const { return mLocation; }
    void SetLocation(std::string location);

    uint32_t GetConfigurationVersion();
    void IncreaseConfigurationVersion();

    chip::app::ServerClusterRegistration &
    CreateBridgedDeviceInfo(chip::EndpointId endpointId,
                            chip::app::Clusters::BridgedDeviceBasicInformationCluster::MutableData && mutableData,
                            chip::app::Clusters::BridgedDeviceBasicInformationCluster::FixedData && fixedData);

    /// Removes the Bridged Device Basic Information registration. Must be
    /// called before the bridge removes the endpoint.
    void Unregister();

    // BridgedDeviceBasicInformationDelegate
    chip::Protocols::InteractionModel::Status OnNodeLabelChanged(const std::string & newNodeLabel) override;

protected:
    char mName[kNameSize + 1]         = {};
    char mUniqueId[kUniqueIdSize + 1] = {};
    std::string mPhysicalId;
    std::string mLocation;
    model::DeviceKind mKind            = model::DeviceKind::kOnOffLight;
    bool mInitialReachable             = false;
    chip::EndpointId mEndpointId       = chip::kInvalidEndpointId;
    chip::EndpointId mParentEndpointId = chip::kInvalidEndpointId;

private:
    chip::app::LazyRegisteredServerCluster<chip::app::Clusters::BridgedDeviceBasicInformationCluster> mBridgedDevice;
};

class BridgedOnOffDevice final : public BridgedDevice
{
public:
    using ChangedCallback = std::function<void(BridgedOnOffDevice &)>;

    BridgedOnOffDevice(const model::DeviceDescriptor & descriptor, std::string uniqueId);

    bool IsOn() const { return mOn; }

    /// Applies state reported by the physical device (or by the mock adapter).
    /// Triggers the change callback only when the value actually changed.
    void ApplyOnOff(bool on);

    void SetChangedCallback(ChangedCallback callback) { mChangedCallback = std::move(callback); }

private:
    bool mOn = false;
    ChangedCallback mChangedCallback;
};

class BridgedTemperatureSensor final : public BridgedDevice
{
public:
    using ChangedCallback = std::function<void(BridgedTemperatureSensor &)>;

    BridgedTemperatureSensor(const model::DeviceDescriptor & descriptor, std::string uniqueId);

    int16_t GetMeasuredValue() const { return mMeasuredValue; }
    int16_t GetMinMeasuredValue() const { return mMinMeasuredValue; }
    int16_t GetMaxMeasuredValue() const { return mMaxMeasuredValue; }

    /// Applies state reported by the physical device and clamps it to the
    /// declared min/max range.
    void ApplyMeasuredValue(int16_t measuredValue);

    void SetChangedCallback(ChangedCallback callback) { mChangedCallback = std::move(callback); }

private:
    int16_t mMinMeasuredValue = 0;
    int16_t mMaxMeasuredValue = 0;
    int16_t mMeasuredValue    = 0;
    ChangedCallback mChangedCallback;
};

} // namespace dxh::bridge

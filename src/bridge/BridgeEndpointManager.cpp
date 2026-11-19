/*
 *    Copyright (c) 2026 DUWI Matter Bridge Authors
 *    SPDX-License-Identifier: Apache-2.0
 *
 * Dynamic endpoint declarations are derived from the Matter SDK bridge
 * example (examples/bridge-app/linux/main.cpp), Copyright (c) 2021 Project
 * CHIP Authors, licensed under Apache-2.0.
 */

#include "BridgeEndpointManager.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/util/endpoint-config-api.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <cstring>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

namespace dxh::bridge {

namespace {

constexpr uint16_t kDeviceVersionDefault = 1;

// Device type ids from the Matter device library.
constexpr DeviceTypeId kDeviceTypeBridgedNode       = 0x0013;
constexpr DeviceTypeId kDeviceTypeOnOffLight        = 0x0100;
constexpr DeviceTypeId kDeviceTypeTemperatureSensor = 0x0302;

// Current ZCL struct encoding uses a fixed-size array of 254 bytes.
constexpr int kDescriptorAttributeArraySize = 254;

// ---------------------------------------------------------------------------
// On/Off light endpoint
//   OnOff, Descriptor, Bridged Device Basic Information (registered at runtime)

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(kOnOffAttributes)
DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OnOff::Id, BOOLEAN, 1, 0), DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(kDescriptorAttributes)
DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::DeviceTypeList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ServerList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ClientList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::PartsList::Id, ARRAY, kDescriptorAttributeArraySize, 0),
#if CHIP_CONFIG_USE_ENDPOINT_UNIQUE_ID
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::EndpointUniqueID::Id, ARRAY, 32, 0),
#endif
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

constexpr CommandId kOnOffIncomingCommands[] = {
    OnOff::Commands::Off::Id,
    OnOff::Commands::On::Id,
    OnOff::Commands::Toggle::Id,
    OnOff::Commands::OffWithEffect::Id,
    OnOff::Commands::OnWithRecallGlobalScene::Id,
    OnOff::Commands::OnWithTimedOff::Id,
    kInvalidCommandId,
};

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(kOnOffClusters)
DECLARE_DYNAMIC_CLUSTER(OnOff::Id, kOnOffAttributes, ZAP_CLUSTER_MASK(SERVER), kOnOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, kDescriptorAttributes, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_ENDPOINT(kOnOffEndpoint, kOnOffClusters);

// ---------------------------------------------------------------------------
// Temperature sensor endpoint

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(kTemperatureAttributes)
DECLARE_DYNAMIC_ATTRIBUTE(TemperatureMeasurement::Attributes::MeasuredValue::Id, INT16S, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(TemperatureMeasurement::Attributes::MinMeasuredValue::Id, INT16S, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(TemperatureMeasurement::Attributes::MaxMeasuredValue::Id, INT16S, 2, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(TemperatureMeasurement::Attributes::FeatureMap::Id, BITMAP32, 4, 0),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(kTemperatureClusters)
DECLARE_DYNAMIC_CLUSTER(TemperatureMeasurement::Id, kTemperatureAttributes, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, kDescriptorAttributes, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER_LIST_END;

DECLARE_DYNAMIC_ENDPOINT(kTemperatureEndpoint, kTemperatureClusters);

// ---------------------------------------------------------------------------

const EmberAfDeviceType kOnOffDeviceTypes[] = {
    { kDeviceTypeOnOffLight, kDeviceVersionDefault },
    { kDeviceTypeBridgedNode, kDeviceVersionDefault },
};

const EmberAfDeviceType kTemperatureDeviceTypes[] = {
    { kDeviceTypeTemperatureSensor, kDeviceVersionDefault },
    { kDeviceTypeBridgedNode, kDeviceVersionDefault },
};

} // namespace

BridgeEndpointManager & BridgeEndpointManager::Instance()
{
    static BridgeEndpointManager instance;
    return instance;
}

void BridgeEndpointManager::Initialize()
{
    const uint16_t fixedEndpointCount = emberAfFixedEndpointCount();
    VerifyOrReturn(fixedEndpointCount > 0, ChipLogError(DeviceLayer, "No fixed endpoints defined by the data model"));

    const EndpointId lastFixedEndpoint = emberAfEndpointFromIndex(static_cast<uint16_t>(fixedEndpointCount - 1));
    mFirstDynamicEndpoint              = static_cast<EndpointId>(lastFixedEndpoint + 1);
    mNextEndpointId                    = mFirstDynamicEndpoint;

    // The last fixed endpoint is the ZAP placeholder that carries every cluster
    // used by dynamic endpoints. Disable it so that it is not exposed.
    emberAfEndpointEnableDisable(lastFixedEndpoint, false);

    ChipLogProgress(DeviceLayer, "Dynamic bridged endpoints start at %u (placeholder endpoint %u disabled)", mFirstDynamicEndpoint,
                    lastFixedEndpoint);
}

int BridgeEndpointManager::FindFreeSlot() const
{
    for (uint8_t slot = 0; slot < kMaxDevices; slot++)
    {
        if (mDevices[slot] == nullptr)
        {
            return slot;
        }
    }
    return -1;
}

int BridgeEndpointManager::AddDevice(BridgedDevice * device)
{
    VerifyOrReturnValue(device != nullptr, -1);

    const int slot = FindFreeSlot();
    if (slot < 0)
    {
        ChipLogError(DeviceLayer, "No free dynamic endpoint slot (max %u)", kMaxDevices);
        return -1;
    }

    const EmberAfEndpointType * endpointType = nullptr;
    Span<const EmberAfDeviceType> deviceTypes;
    uint8_t clusterCount = 0;

    switch (device->GetKind())
    {
    case model::DeviceKind::kOnOffLight:
        endpointType = &kOnOffEndpoint;
        deviceTypes  = Span<const EmberAfDeviceType>(kOnOffDeviceTypes);
        clusterCount = static_cast<uint8_t>(MATTER_ARRAY_SIZE(kOnOffClusters));
        break;
    case model::DeviceKind::kTemperatureSensor:
        endpointType = &kTemperatureEndpoint;
        deviceTypes  = Span<const EmberAfDeviceType>(kTemperatureDeviceTypes);
        clusterCount = static_cast<uint8_t>(MATTER_ARRAY_SIZE(kTemperatureClusters));
        break;
    }

    VerifyOrReturnValue(endpointType != nullptr, -1);
    VerifyOrReturnValue(clusterCount <= kMaxClustersPerEndpoint, -1);

    const Span<DataVersion> dataVersions(mDataVersions[slot], clusterCount);
    const EndpointId lastDynamicEndpoint = static_cast<EndpointId>(mFirstDynamicEndpoint + kMaxDevices - 1);

    while (true)
    {
        device->SetEndpointId(mNextEndpointId);
        device->SetParentEndpointId(kAggregatorEndpointId);

#if CHIP_CONFIG_USE_ENDPOINT_UNIQUE_ID
        const CHIP_ERROR err = emberAfSetDynamicEndpointWithEpUniqueId(
            static_cast<uint16_t>(slot), mNextEndpointId, endpointType, dataVersions, deviceTypes,
            CharSpan::fromCharString(device->GetUniqueId()), kAggregatorEndpointId);
#else
        const CHIP_ERROR err = emberAfSetDynamicEndpoint(static_cast<uint16_t>(slot), mNextEndpointId, endpointType, dataVersions,
                                                         deviceTypes, kAggregatorEndpointId);
#endif

        if (err == CHIP_NO_ERROR)
        {
            mDevices[slot] = device;
            ChipLogProgress(DeviceLayer, "Added %s device \"%s\" at endpoint %u (slot %d)",
                            model::DeviceKindName(device->GetKind()), device->GetName(), mNextEndpointId, slot);

            // The Bridged Device Basic Information cluster is a code-driven
            // cluster registered on the same endpoint after the endpoint exists.
            LogErrorOnFailure(CodegenDataModelProvider::Instance().Registry().Register(
                device->CreateBridgedDeviceInfo(mNextEndpointId,
                                                {
                                                    .reachable = device->GetInitialReachable(),
                                                    .nodeLabel = device->GetName(),
                                                },
                                                {
                                                    .uniqueId = device->GetUniqueId(),
                                                })));
            return slot;
        }

        if (err != CHIP_ERROR_ENDPOINT_EXISTS)
        {
            ChipLogError(DeviceLayer, "Failed to add dynamic endpoint for \"%s\": %" CHIP_ERROR_FORMAT, device->GetName(),
                         err.Format());
            return -1;
        }

        // Endpoint id already used (for example by a previously added device).
        // Advance within the dynamic range and wrap around at its end.
        if (++mNextEndpointId > lastDynamicEndpoint)
        {
            mNextEndpointId = mFirstDynamicEndpoint;
        }
    }
}

int BridgeEndpointManager::RemoveDevice(BridgedDevice * device)
{
    VerifyOrReturnValue(device != nullptr, -1);

    for (uint8_t slot = 0; slot < kMaxDevices; slot++)
    {
        if (mDevices[slot] != device)
        {
            continue;
        }

        device->Unregister();
        [[maybe_unused]] const EndpointId endpoint = emberAfClearDynamicEndpoint(slot);
        mDevices[slot]                             = nullptr;
        ChipLogProgress(DeviceLayer, "Removed device \"%s\" from endpoint %u (slot %u)", device->GetName(), endpoint, slot);
        return slot;
    }

    return -1;
}

BridgedDevice * BridgeEndpointManager::FindByEndpoint(EndpointId endpoint) const
{
    const uint16_t index = emberAfGetDynamicIndexFromEndpoint(endpoint);
    if (index >= kMaxDevices)
    {
        return nullptr;
    }
    return mDevices[index];
}

BridgedDevice * BridgeEndpointManager::FindByPhysicalId(const std::string & physicalId) const
{
    for (const auto * device : mDevices)
    {
        if (device != nullptr && device->GetPhysicalId() == physicalId)
        {
            return const_cast<BridgedDevice *>(device);
        }
    }
    return nullptr;
}

} // namespace dxh::bridge

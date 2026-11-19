/*
 *    Copyright (c) 2026 DUWI Matter Bridge Authors
 *    SPDX-License-Identifier: Apache-2.0
 *
 * Derived from the Matter SDK bridge example
 * (examples/bridge-app/linux/main.cpp), Copyright (c) 2021 Project CHIP
 * Authors, licensed under Apache-2.0.
 */

#include "BridgedAttributeAccess.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/util/af-types.h>
#include <app/util/attribute-storage.h>
#include <lib/support/logging/CHIPLogging.h>

#include <cstring>

#include "BridgeEndpointManager.h"
#include "BridgedDevice.h"

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using chip::Protocols::InteractionModel::Status;

namespace dxh::bridge {

// Cluster revisions exposed by the dynamic endpoints. Keep these aligned with
// the revisions implemented by the SDK.
constexpr uint16_t kOnOffClusterRevision                  = 4;
constexpr uint16_t kTemperatureMeasurementClusterRevision = 1;
constexpr uint32_t kTemperatureMeasurementFeatureMap      = 0;

OnOffWriteHandler gOnOffWriteHandler = nullptr;

Status ReadOnOffAttribute(BridgedOnOffDevice & device, AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
    switch (attributeId)
    {
    case OnOff::Attributes::OnOff::Id:
        VerifyOrReturnValue(maxReadLength == sizeof(uint8_t), Status::Failure);
        *buffer = device.IsOn() ? 1 : 0;
        return Status::Success;
    case OnOff::Attributes::ClusterRevision::Id: {
        VerifyOrReturnValue(maxReadLength == sizeof(uint16_t), Status::Failure);
        const uint16_t revision = kOnOffClusterRevision;
        std::memcpy(buffer, &revision, sizeof(revision));
        return Status::Success;
    }
    default:
        return Status::Failure;
    }
}

Status ReadTemperatureAttribute(BridgedTemperatureSensor & device, AttributeId attributeId, uint8_t * buffer,
                                uint16_t maxReadLength)
{
    using namespace TemperatureMeasurement::Attributes;

    switch (attributeId)
    {
    case MeasuredValue::Id: {
        VerifyOrReturnValue(maxReadLength == sizeof(int16_t), Status::Failure);
        const int16_t value = device.GetMeasuredValue();
        std::memcpy(buffer, &value, sizeof(value));
        return Status::Success;
    }
    case MinMeasuredValue::Id: {
        VerifyOrReturnValue(maxReadLength == sizeof(int16_t), Status::Failure);
        const int16_t value = device.GetMinMeasuredValue();
        std::memcpy(buffer, &value, sizeof(value));
        return Status::Success;
    }
    case MaxMeasuredValue::Id: {
        VerifyOrReturnValue(maxReadLength == sizeof(int16_t), Status::Failure);
        const int16_t value = device.GetMaxMeasuredValue();
        std::memcpy(buffer, &value, sizeof(value));
        return Status::Success;
    }
    case FeatureMap::Id: {
        VerifyOrReturnValue(maxReadLength == sizeof(uint32_t), Status::Failure);
        const uint32_t value = kTemperatureMeasurementFeatureMap;
        std::memcpy(buffer, &value, sizeof(value));
        return Status::Success;
    }
    case ClusterRevision::Id: {
        VerifyOrReturnValue(maxReadLength == sizeof(uint16_t), Status::Failure);
        const uint16_t value = kTemperatureMeasurementClusterRevision;
        std::memcpy(buffer, &value, sizeof(value));
        return Status::Success;
    }
    default:
        return Status::Failure;
    }
}

void SetOnOffWriteHandler(OnOffWriteHandler handler)
{
    gOnOffWriteHandler = handler;
}

} // namespace dxh::bridge

// ---------------------------------------------------------------------------
// Ember external attribute callbacks. The SDK requires these exact symbols for
// attributes declared with MATTER_ATTRIBUTE_FLAG_EXTERNAL_STORAGE.

Protocols::InteractionModel::Status emberAfExternalAttributeReadCallback(EndpointId endpoint, ClusterId clusterId,
                                                                         const EmberAfAttributeMetadata * attributeMetadata,
                                                                         uint8_t * buffer, uint16_t maxReadLength)
{
    VerifyOrReturnValue(attributeMetadata != nullptr && buffer != nullptr, Status::Failure);

    auto * device = dxh::bridge::BridgeEndpointManager::Instance().FindByEndpoint(endpoint);
    VerifyOrReturnValue(device != nullptr, Status::Failure);

    if (clusterId == OnOff::Id && device->GetKind() == dxh::model::DeviceKind::kOnOffLight)
    {
        return dxh::bridge::ReadOnOffAttribute(*static_cast<dxh::bridge::BridgedOnOffDevice *>(device),
                                               attributeMetadata->attributeId, buffer, maxReadLength);
    }

    if (clusterId == TemperatureMeasurement::Id && device->GetKind() == dxh::model::DeviceKind::kTemperatureSensor)
    {
        return dxh::bridge::ReadTemperatureAttribute(*static_cast<dxh::bridge::BridgedTemperatureSensor *>(device),
                                                     attributeMetadata->attributeId, buffer, maxReadLength);
    }

    return Status::Failure;
}

Protocols::InteractionModel::Status emberAfExternalAttributeWriteCallback(EndpointId endpoint, ClusterId clusterId,
                                                                          const EmberAfAttributeMetadata * attributeMetadata,
                                                                          uint8_t * buffer)
{
    VerifyOrReturnValue(attributeMetadata != nullptr && buffer != nullptr, Status::Failure);

    auto * device = dxh::bridge::BridgeEndpointManager::Instance().FindByEndpoint(endpoint);
    VerifyOrReturnValue(device != nullptr, Status::Failure);

    if (clusterId != OnOff::Id || device->GetKind() != dxh::model::DeviceKind::kOnOffLight)
    {
        return Status::Failure;
    }
    if (attributeMetadata->attributeId != OnOff::Attributes::OnOff::Id)
    {
        return Status::Failure;
    }

    auto & onOffDevice = *static_cast<dxh::bridge::BridgedOnOffDevice *>(device);
    VerifyOrReturnValue(onOffDevice.IsReachable(), Status::Failure);

    const dxh::bridge::OnOffWriteHandler handler = dxh::bridge::gOnOffWriteHandler;
    VerifyOrReturnValue(handler != nullptr, Status::Failure);

    const bool on = (*buffer != 0);
    ChipLogProgress(DeviceLayer, "Matter write: OnOff=%d for \"%s\"", on ? 1 : 0, onOffDevice.GetName());
    return handler(onOffDevice, on);
}

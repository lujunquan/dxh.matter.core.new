/*
 *    Copyright (c) 2026 DUWI Matter Bridge Authors
 *    SPDX-License-Identifier: Apache-2.0
 *
 * Owns the dynamic endpoints exposed to Matter and maps endpoint ids back to
 * bridged devices.
 */

#pragma once

#include <app/util/attribute-storage.h>
#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>

#include <cstdint>
#include <string>

#include "BridgedDevice.h"

namespace dxh::bridge {

class BridgeEndpointManager
{
public:
    static constexpr uint8_t kMaxDevices             = CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT;
    static constexpr uint8_t kMaxClustersPerEndpoint = 4;

    /// Fixed endpoint that acts as the parent (aggregator) of all bridged
    /// devices. It is defined in zap/dxh-bridge.zap.
    static constexpr chip::EndpointId kAggregatorEndpointId = 1;

    static BridgeEndpointManager & Instance();

    /// Computes the dynamic endpoint range and disables the last fixed
    /// endpoint, which is the ZAP placeholder carrying the clusters used by
    /// dynamic endpoints. Must be called after Matter stack initialization.
    void Initialize();

    /// Adds a device as a dynamic endpoint.
    /// @return the dynamic index (0-based) on success, -1 on failure.
    int AddDevice(BridgedDevice * device);

    /// Removes a device and its endpoint.
    /// @return the dynamic index that was freed, -1 if the device was unknown.
    int RemoveDevice(BridgedDevice * device);

    BridgedDevice * FindByEndpoint(chip::EndpointId endpoint) const;
    BridgedDevice * FindByPhysicalId(const std::string & physicalId) const;

private:
    BridgeEndpointManager() = default;

    int FindFreeSlot() const;

    BridgedDevice * mDevices[kMaxDevices]                                 = {};
    chip::DataVersion mDataVersions[kMaxDevices][kMaxClustersPerEndpoint] = {};
    chip::EndpointId mNextEndpointId                                      = chip::kInvalidEndpointId;
    chip::EndpointId mFirstDynamicEndpoint                                = chip::kInvalidEndpointId;
};

} // namespace dxh::bridge

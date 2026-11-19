/*
 *    Copyright (c) 2026 DUWI Matter Bridge Authors
 *    SPDX-License-Identifier: Apache-2.0
 *
 * Deterministic Bridged Device Basic Information UniqueID derivation.
 * Pure C++: no connectedhomeip dependency, unit tested on the host.
 */

#pragma once

#include <string>

namespace dxh::bridge {

/// Builds a 32-character uppercase hexadecimal UniqueID from the adapter
/// identity and the physical device id.
///
/// The result must be stable across reboots and firmware upgrades, because the
/// Matter specification requires a bridged device's UniqueID to remain fixed
/// once it has been exposed (changing it requires a ConfigurationVersion bump).
std::string MakeStableUniqueId(const std::string & adapterName, const std::string & physicalId);

} // namespace dxh::bridge

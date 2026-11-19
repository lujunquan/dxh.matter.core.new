/*
 *    Copyright (c) 2026 DUWI Matter Bridge Authors
 *    SPDX-License-Identifier: Apache-2.0
 *
 * Adapter between the ember external-attribute callbacks and the bridged
 * device objects. Commands are forwarded to the application layer through a
 * function pointer so this file stays independent from the concrete adapter.
 */

#pragma once

#include <protocols/interaction_model/StatusCode.h>

namespace dxh::bridge {

class BridgedOnOffDevice;

/// Handler invoked when a Matter controller writes the OnOff attribute.
using OnOffWriteHandler = chip::Protocols::InteractionModel::Status (*)(BridgedOnOffDevice & device, bool on);

void SetOnOffWriteHandler(OnOffWriteHandler handler);

} // namespace dxh::bridge

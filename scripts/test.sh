#!/usr/bin/env bash
#
# Copyright (c) 2026 DUWI Matter Bridge Authors
# SPDX-License-Identifier: Apache-2.0
#
# Host-only unit tests for the SDK-independent core. No connectedhomeip
# checkout, gn, ninja or CIPD environment is required.
#
# Usage:
#   scripts/test.sh
#   CXX=clang++ scripts/test.sh

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
OUT_DIR="${DXH_TEST_OUT_DIR:-${ROOT}/out/tests}"
BINARY="${OUT_DIR}/dxh-core-tests"

mkdir -p "${OUT_DIR}"

SOURCES=(
    "${ROOT}/tests/test_core.cpp"
    "${ROOT}/src/adapters/MockDeviceAdapter.cpp"
    "${ROOT}/src/bridge/StableId.cpp"
)

echo "Compiling core tests with ${CXX}"
"${CXX}" \
    -std=c++17 \
    -Wall -Wextra -Werror -pedantic \
    -I"${ROOT}/src" \
    "${SOURCES[@]}" \
    -o "${BINARY}"

"${BINARY}"
#!/usr/bin/env bash
#
# Copyright (c) 2026 DUWI Matter Bridge Authors
# SPDX-License-Identifier: Apache-2.0
#
# Links this out-of-tree project to an external connectedhomeip checkout.
# Idempotent: safe to run on every build.
#
# Environment:
#   MATTER_SDK_ROOT  Path to the connectedhomeip checkout.
#                    Defaults to ../connectedhomeip (sibling directory).
#
# GN visibility note:
#   Some SDK targets (for example src/app:required-privileges) are only visible
#   to targets under ${chip_root}/examples/*. A project-owned data model
#   therefore has to be addressed through the SDK's examples namespace, exactly
#   like the in-tree example apps do. This script creates the untracked symlink
#     <sdk>/examples/dxh-matter-core -> <project root>
#   so the data model can be referenced as
#   ${chip_root}/examples/dxh-matter-core/zap:dxh-bridge-data-model.
#   The SDK working tree is not otherwise modified. Use --clean to remove it.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_INPUT="${MATTER_SDK_ROOT:-${ROOT}/../connectedhomeip}"

if [[ ! -d "${SDK_INPUT}" ]]; then
    echo "error: connectedhomeip checkout not found at ${SDK_INPUT}" >&2
    echo "       set MATTER_SDK_ROOT to the correct path" >&2
    exit 1
fi

SDK_ROOT="$(cd "${SDK_INPUT}" && pwd -P)"
SDK_EXAMPLE_LINK="${SDK_ROOT}/examples/dxh-matter-core"

if [[ "${1:-}" == "--clean" ]]; then
    rm -f "${SDK_EXAMPLE_LINK}" "${ROOT}/build_overrides" "${ROOT}/third_party/connectedhomeip"
    echo "Removed generated symlinks."
    exit 0
fi

required_files=(
    "examples/build_overrides/build.gni"
    "examples/build_overrides/chip.gni"
    "config/standalone/args.gni"
    "scripts/activate.sh"
)

for required in "${required_files[@]}"; do
    if [[ ! -e "${SDK_ROOT}/${required}" ]]; then
        echo "error: ${SDK_ROOT} does not look like a connectedhomeip checkout" >&2
        echo "       missing: ${required}" >&2
        exit 1
    fi
done

mkdir -p "${ROOT}/third_party"

# GN resolves //build_overrides/* through this link, and //third_party/connectedhomeip
# is the chip_root used by every BUILD.gn.
ln -sfn "${SDK_ROOT}" "${ROOT}/third_party/connectedhomeip"
ln -sfn "${SDK_ROOT}/examples/build_overrides" "${ROOT}/build_overrides"

# Required for GN visibility rules; see the note at the top of this script.
ln -sfn "${ROOT}" "${SDK_EXAMPLE_LINK}"

echo "connectedhomeip : ${SDK_ROOT}"
echo "build_overrides : ${ROOT}/build_overrides -> ${SDK_ROOT}/examples/build_overrides"
echo "chip_root       : ${ROOT}/third_party/connectedhomeip"
echo "sdk example link: ${SDK_EXAMPLE_LINK} -> ${ROOT}"
#!/usr/bin/env bash
#
# Copyright (c) 2026 DUWI Matter Bridge Authors
# SPDX-License-Identifier: Apache-2.0
#
# Builds the DUWI Matter bridge against the linked connectedhomeip checkout.
#
# Usage:
#   scripts/build.sh                 # build out/debug
#   scripts/build.sh --clean         # remove out/ first
#   DXH_OUT_DIR=out/asan DXH_GN_ARGS='is_asan=true' scripts/build.sh
#
# Extra arguments are forwarded to ninja.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${DXH_OUT_DIR:-out/debug}"

CLEAN=0
NINJA_ARGS=()
for arg in "$@"; do
    case "${arg}" in
    --clean)
        CLEAN=1
        ;;
    *)
        NINJA_ARGS+=("${arg}")
        ;;
    esac
done

"${ROOT}/scripts/setup.sh"

SDK_ROOT="$(cd "${ROOT}/third_party/connectedhomeip" && pwd -P)"

# Provides gn, ninja, zap and the host toolchain used by the Matter build.
# activate.sh is not nounset-safe, so relax `set -u` while sourcing it.
# It also runs `git submodule status` relative to the current directory, so
# enter the SDK first and return to the project afterwards.
cd "${SDK_ROOT}"
set +u
# shellcheck disable=SC1090
source "${SDK_ROOT}/scripts/activate.sh" >/dev/null
set -u

cd "${ROOT}"

if [[ "${CLEAN}" -eq 1 ]]; then
    rm -rf "${OUT_DIR}"
fi

GN_ARGS=()
if [[ -n "${DXH_GN_ARGS:-}" ]]; then
    GN_ARGS+=("--args=${DXH_GN_ARGS}")
fi

# Empty-array expansion must be guarded on bash 3.2 (macOS /bin/bash).
gn gen "${OUT_DIR}" ${GN_ARGS[@]+"${GN_ARGS[@]}"}
ninja -C "${OUT_DIR}" ${NINJA_ARGS[@]+"${NINJA_ARGS[@]}"}

# Darwin's linker does not embed DWARF into the executable, so lldb (and thus
# VS Code / CodeLLDB) cannot resolve source-line breakpoints unless a companion
# .dSYM sits next to the binary. Ninja keeps the object files around, so this
# step is cheap and idempotent. ELF platforms need no equivalent.
if [[ "$(uname -s)" == "Darwin" && -f "${OUT_DIR}/dxh-matter-bridge" ]]; then
    if ! dsymutil "${OUT_DIR}/dxh-matter-bridge" -o "${OUT_DIR}/dxh-matter-bridge.dSYM"; then
        echo "warning: dsymutil failed; source-line breakpoints may not resolve" >&2
    fi
fi

echo
echo "Built: ${ROOT}/${OUT_DIR}/dxh-matter-bridge"
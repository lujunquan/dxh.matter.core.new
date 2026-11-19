#!/usr/bin/env bash
#
# Copyright (c) 2026 DUWI Matter Bridge Authors
# SPDX-License-Identifier: Apache-2.0
#
# Prepares the connectedhomeip submodule and the symlinks the GN build needs.
# Idempotent: safe to run on every build.
#
# The SDK is a git submodule of this repository:
#   third_party/connectedhomeip -> git@github.com:lujunquan/connectedhomeip.git
#
# Usage:
#   ./scripts/setup.sh            # initialize submodule + create symlinks
#   ./scripts/setup.sh --clean    # remove generated symlinks
#   ./scripts/setup.sh --deinit   # ... and deinitialize the SDK submodule
#
# GN visibility note:
#   Some SDK targets (for example src/app:required-privileges) are only visible
#   to targets under ${chip_root}/examples/*. A project-owned data model
#   therefore has to be addressed through the SDK's examples namespace, exactly
#   like the in-tree example apps do. This script creates the untracked symlink
#     <sdk>/examples/dxh-matter-core -> <project root>
#   so the data model can be referenced as
#   ${chip_root}/examples/dxh-matter-core/zap:dxh-bridge-data-model.
#   The symlink is registered in the submodule's local exclude file so it never
#   shows up as submodule dirt. The SDK working tree is not otherwise modified.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_DIR="${ROOT}/third_party/connectedhomeip"
SDK_EXAMPLE_LINK_NAME="dxh-matter-core"

sdk_git_dir() {
    local git_dir
    git_dir="$(git -C "${SDK_DIR}" rev-parse --git-dir 2>/dev/null || true)"
    case "${git_dir}" in
    "") return 1 ;;
    /*) printf '%s\n' "${git_dir}" ;;
    *) printf '%s\n' "${SDK_DIR}/${git_dir}" ;;
    esac
}

remove_generated_links() {
    rm -f "${ROOT}/build_overrides"

    if [[ -d "${SDK_DIR}" && ! -L "${SDK_DIR}" ]]; then
        local git_dir
        if git_dir="$(sdk_git_dir)"; then
            rm -f "${SDK_DIR}/examples/${SDK_EXAMPLE_LINK_NAME}"
            if [[ -f "${git_dir}/info/exclude" ]]; then
                grep -vxF "examples/${SDK_EXAMPLE_LINK_NAME}" "${git_dir}/info/exclude" \
                    >"${git_dir}/info/exclude.tmp" || true
                mv "${git_dir}/info/exclude.tmp" "${git_dir}/info/exclude"
            fi
        fi
    fi
}

case "${1:-}" in
--clean)
    remove_generated_links
    echo "Removed generated symlinks."
    exit 0
    ;;
--deinit)
    remove_generated_links
    if [[ -f "${ROOT}/.gitmodules" ]]; then
        git -C "${ROOT}" submodule deinit -f -- third_party/connectedhomeip || true
    fi
    echo "Removed generated symlinks and deinitialized the SDK submodule."
    exit 0
    ;;
esac

# --- SDK submodule ----------------------------------------------------------

if [[ -L "${SDK_DIR}" ]]; then
    echo "error: ${SDK_DIR} is still a symlink from the old external-checkout setup." >&2
    echo "       remove it, then run: git submodule update --init --recursive" >&2
    exit 1
fi

if [[ ! -f "${SDK_DIR}/scripts/activate.sh" ]]; then
    if [[ ! -d "${SDK_DIR}" || -z "$(ls -A "${SDK_DIR}" 2>/dev/null)" ]]; then
        echo "Initializing connectedhomeip submodule..."
        git -C "${ROOT}" submodule update --init third_party/connectedhomeip
    fi
fi

if [[ ! -f "${SDK_DIR}/scripts/activate.sh" ]]; then
    echo "error: ${SDK_DIR} is not a connectedhomeip checkout." >&2
    echo "       run: git submodule update --init --recursive" >&2
    exit 1
fi

SDK_ROOT="$(cd "${SDK_DIR}" && pwd -P)"

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

# Submodules that the SDK's own activate.sh checks on every shell activation.
# Platform-specific vendor SDKs are managed separately by the SDK's
# scripts/checkout_submodules.py and are intentionally not fetched here.
required_submodules=(
    "third_party/pigweed/repo"
    "third_party/openthread/repo"
    "third_party/editline/repo"
)

missing_submodules=()
for submodule_path in "${required_submodules[@]}"; do
    submodule_status="$(git -C "${SDK_ROOT}" submodule status -- "${submodule_path}" 2>/dev/null || true)"
    if [[ -z "${submodule_status}" || "${submodule_status}" == -* ]]; then
        missing_submodules+=("${submodule_path}")
    fi
done

if [[ "${#missing_submodules[@]}" -gt 0 ]]; then
    echo "Initializing connectedhomeip submodules: ${missing_submodules[*]}"
    git -C "${SDK_ROOT}" submodule update --init --recursive -- "${missing_submodules[@]}"
fi

nested_state="$(git -C "${SDK_ROOT}" submodule status 2>/dev/null || true)"
if grep -q '^-' <<<"${nested_state}"; then
    echo "note: some connectedhomeip submodules are not initialized (vendor SDKs)."
    echo "      for a full SDK checkout run:"
    echo "      ${SDK_ROOT}/scripts/checkout_submodules.py --shallow --platform <platform>"
fi

# --- Generated symlinks -----------------------------------------------------

# GN resolves //build_overrides/* through this link, and //third_party/connectedhomeip
# is the chip_root used by every BUILD.gn.
ln -sfn "${SDK_ROOT}/examples/build_overrides" "${ROOT}/build_overrides"

# Required for GN visibility rules; see the note at the top of this script.
rm -f "${SDK_ROOT}/examples/${SDK_EXAMPLE_LINK_NAME}"
ln -sfn "${ROOT}" "${SDK_ROOT}/examples/${SDK_EXAMPLE_LINK_NAME}"

# Keep the in-SDK symlink out of `git status` without touching tracked files.
if git_dir="$(sdk_git_dir)"; then
    mkdir -p "${git_dir}/info"
    if ! grep -qxF "examples/${SDK_EXAMPLE_LINK_NAME}" "${git_dir}/info/exclude" 2>/dev/null; then
        printf 'examples/%s\n' "${SDK_EXAMPLE_LINK_NAME}" >>"${git_dir}/info/exclude"
    fi
fi

echo "connectedhomeip : ${SDK_ROOT}"
echo "build_overrides : ${ROOT}/build_overrides -> ${SDK_ROOT}/examples/build_overrides"
echo "chip_root       : ${ROOT}/third_party/connectedhomeip"
echo "sdk example link: ${SDK_ROOT}/examples/${SDK_EXAMPLE_LINK_NAME} -> ${ROOT}"
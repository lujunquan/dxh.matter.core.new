#!/usr/bin/env bash
#
# Copyright (c) 2026 DUWI Matter Bridge Authors
# SPDX-License-Identifier: Apache-2.0
#
# Runs the bridge with development defaults. Any arguments are forwarded to the
# application, e.g.:
#
#   scripts/run.sh --trace-to json:/tmp/dxh.trace.json
#
# Development defaults (only used when no argument is given):
#   --discriminator 1234 --passcode 20202021
#   --KVS /tmp/dxh-matter-bridge.kvs

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="${DXH_BINARY:-${ROOT}/out/debug/dxh-matter-bridge}"

if [[ ! -x "${BINARY}" ]]; then
    echo "error: ${BINARY} not found. Run scripts/build.sh first." >&2
    exit 1
fi

if [[ "$#" -eq 0 ]]; then
    set -- \
        --discriminator 1234 \
        --passcode 20202021 \
        --KVS "${DXH_KVS:-/tmp/dxh-matter-bridge.kvs}"
fi

exec "${BINARY}" "$@"
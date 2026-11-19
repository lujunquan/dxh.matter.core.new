#!/usr/bin/env bash
#
# Copyright (c) 2026 DUWI Matter Bridge Authors
# SPDX-License-Identifier: Apache-2.0
#
# Formats all project C++ sources with clang-format. Pass --check to only
# verify formatting.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"

if ! command -v "${CLANG_FORMAT}" >/dev/null 2>&1; then
    echo "error: ${CLANG_FORMAT} not found in PATH" >&2
    exit 1
fi

MODE="--in-place"
if [[ "${1:-}" == "--check" ]]; then
    MODE="--dry-run"
    CHECK_ARGS=(--Werror)
fi

FILES=()
while IFS= read -r file; do
    FILES+=("${file}")
done < <(find "${ROOT}/src" "${ROOT}/tests" -type f \( -name '*.cpp' -o -name '*.h' \) | sort)

if [[ "${#FILES[@]}" -eq 0 ]]; then
    echo "No C++ sources found."
    exit 0
fi

if [[ "${MODE}" == "--in-place" ]]; then
    "${CLANG_FORMAT}" -i "${FILES[@]}"
else
    "${CLANG_FORMAT}" "${MODE}" "${CHECK_ARGS[@]}" "${FILES[@]}"
fi
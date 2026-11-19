/*
 *    Copyright (c) 2026 DUWI Matter Bridge Authors
 *    SPDX-License-Identifier: Apache-2.0
 */

#include "StableId.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace dxh::bridge {

namespace {

constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime       = 1099511628211ULL;

// Second stream uses a different offset basis so that concatenating the two
// hashes gives 128 bits of spread while still fitting the 32-character
// UniqueID limit.
constexpr uint64_t kSecondOffsetBasis = 0x9E3779B97F4A7C15ULL;

uint64_t Fnv1a(uint64_t hash, const std::string & value)
{
    for (const char c : value)
    {
        hash ^= static_cast<uint8_t>(c);
        hash *= kFnvPrime;
    }
    return hash;
}

} // namespace

std::string MakeStableUniqueId(const std::string & adapterName, const std::string & physicalId)
{
    // Domain-separate the two streams: stream A hashes "adapter#physicalId",
    // stream B hashes "physicalId#adapter".
    const uint64_t first  = Fnv1a(Fnv1a(kFnvOffsetBasis, adapterName), "#" + physicalId);
    const uint64_t second = Fnv1a(Fnv1a(kSecondOffsetBasis, physicalId), "#" + adapterName);

    char buffer[33];
    std::snprintf(buffer, sizeof(buffer), "%016llX%016llX", static_cast<unsigned long long>(first),
                  static_cast<unsigned long long>(second));
    return std::string(buffer);
}

} // namespace dxh::bridge

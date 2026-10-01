#pragma once

#include <cstddef>
#include <cstdint>

// Rank cache
inline constexpr std::int64_t kRankCacheTtlSeconds = 604800; // 7 days

// Rival cache (same default TTL as rank cache for now)
inline constexpr std::int64_t kRivalCacheTtlSeconds = 604800; // 7 days

// Rival snapshots can be much larger than board/ghost payloads.
inline constexpr int kHttpGetReceiveTimeoutMs = 180000; // 3 minutes
// ~41k scores is about 6MB today; 12MB leaves headroom without unbounded reads.
inline constexpr std::size_t kHttpGetMaxResponseBytes = 12 * 1024 * 1024;

// Auth / logging
inline constexpr std::size_t kApiKeyMaxLen = 32;
inline constexpr std::uintmax_t kDebugLogRotateBytes = 2 * 1024 * 1024;

// Clear lamps (LR2 order)
inline constexpr const char* kLamps[] = {
    "NO PLAY", "FAIL", "EASY", "NORMAL", "HARD", "FULL COMBO"
};

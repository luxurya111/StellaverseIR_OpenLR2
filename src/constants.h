#pragma once

#include <cstddef>
#include <cstdint>

// Rank cache
inline constexpr std::int64_t kRankCacheTtlSeconds = 604800; // 7 days

// Auth / logging
inline constexpr std::size_t kApiKeyMaxLen = 32;
inline constexpr std::uintmax_t kDebugLogRotateBytes = 2 * 1024 * 1024;

// Clear lamps (LR2 order)
inline constexpr const char* kLamps[] = {
    "NO PLAY", "FAIL", "EASY", "NORMAL", "HARD", "FULL COMBO"
};

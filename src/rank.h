#pragma once

#include <LR2_customir_api.h>

#include "http.h"

#include <string>
#include <string_view>

void InvalidateChartRankCaches(std::string_view hash);
bool TryReadRankCache(std::string_view hash, openlr2::IRRankResult& out);

HttpStatus GetRankFromNetwork(
    std::string_view songHash,
    const std::string& apiKey,
    openlr2::IRRankResult& out
);

HttpStatus GetGhostFromNetwork(
    std::string_view songHash,
    openlr2::GhostMode mode,
    int targetPlayerId,
    const std::string& apiKey,
    openlr2::IRGhostResult& out
);

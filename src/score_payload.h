#pragma once

#include "score_db.h"

#include <string>
#include <string_view>

std::string BuildScoreJson(
    const IRScoreV1& score,
    std::string_view ghostDataForPost,
    bool isPb,
    bool hasDbRow,
    const LocalScoreRow* dbRow,
    const LocalScoreRow* previousBest
);

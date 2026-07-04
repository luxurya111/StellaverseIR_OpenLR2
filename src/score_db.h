#pragma once

#include <LR2_customir_api.h>

#include <string>
#include <string_view>

struct LocalScoreRow {
    int clear{};
    int perfect{};
    int great{};
    int good{};
    int bad{};
    int poor{};
    int totalnotes{};
    int maxcombo{};
    int minbp{};
    int playcount{};
    int clearcount{};
    int failcount{};
    int rank{};
    int rate{};
    int clear_db{};
    int clear_sd{};
    int clear_ex{};
    int op_best{};
    int rseed{};
    std::string ghost{};
};

struct ResolvedGhostForPost {
    std::string data;
    const char* source = "none";
};

bool QueryLocalScore(const std::string& songHash, LocalScoreRow& out);
bool DetectPersonalBest(const IRScoreV1& score, const LocalScoreRow* dbRow, bool hasDbRow);
ResolvedGhostForPost ResolveGhostDataForPost(
    std::string_view liveGhostData,
    bool hasDbRow,
    const LocalScoreRow* dbRow
);

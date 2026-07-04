#include "score_db.h"

#include "log.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>

#include <sqlite3.h>

bool DetectPersonalBest(const IRScoreV1& score, const LocalScoreRow* dbRow, bool hasDbRow) {
    if (!hasDbRow || dbRow == nullptr) {
        return true;
    }
    const int currentEx = score.exscore;
    const int dbEx = dbRow->great + dbRow->perfect * 2;
    if (currentEx != dbEx) {
        return currentEx > dbEx;
    }
    const auto& j = score.judgements_total;
    return (j.epg + j.lpg) == static_cast<unsigned>(dbRow->perfect)
        && (j.egr + j.lgr) == static_cast<unsigned>(dbRow->great)
        && (j.egd + j.lgd) == static_cast<unsigned>(dbRow->good)
        && (j.ebd + j.lbd) == static_cast<unsigned>(dbRow->bad)
        && (j.epr + j.lpr) == static_cast<unsigned>(dbRow->poor);
}

ResolvedGhostForPost ResolveGhostDataForPost(
    std::string_view liveGhostData,
    bool hasDbRow,
    const LocalScoreRow* dbRow
) {
    ResolvedGhostForPost resolved;
    const auto valid = [](std::string_view ghost) {
        return !ghost.empty() && ghost != "GHOST_ERROR" && ghost != "Z";
    };
    if (valid(liveGhostData)) {
        resolved.data = std::string(liveGhostData);
        resolved.source = "current";
        return resolved;
    }
    if (hasDbRow && dbRow != nullptr && valid(dbRow->ghost)) {
        resolved.data = dbRow->ghost;
        resolved.source = "db";
    }
    return resolved;
}

bool QueryLocalScore(const std::string& songHash, LocalScoreRow& out) {
    std::string playerId;
    {
        std::ifstream file("LR2files/Config/config.xml");
        if (file) {
            const std::string xml((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            std::size_t searchFrom = xml.find("<player>");
            if (searchFrom == std::string::npos) {
                searchFrom = 0;
            }
            const std::size_t start = xml.find("<id>", searchFrom);
            if (start != std::string::npos) {
                const std::size_t contentStart = start + 4;
                const std::size_t end = xml.find("</id>", contentStart);
                if (end != std::string::npos) {
                    playerId = xml.substr(contentStart, end - contentStart);
                }
            }
        }
    }

    if (playerId.empty() || songHash.empty()) {
        DebugLog("WARN", "db_query", std::format("result=no_path hash8={}", HashPrefix8(songHash)));
        return false;
    }

    const auto dbPath = std::filesystem::path("LR2files") / "Database" / "Score" / std::format("{}.db", playerId);
    sqlite3* db = nullptr;
    const int openRc = sqlite3_open_v2(dbPath.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (openRc != SQLITE_OK) {
        const char* errMsg = db != nullptr ? sqlite3_errmsg(db) : sqlite3_errstr(openRc);
        DebugLog("WARN", "db_query", std::format("result=open_fail sqlite={}", errMsg != nullptr ? errMsg : "unknown"));
        if (db) {
            sqlite3_close(db);
        }
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    constexpr const char* query =
        "SELECT clear, perfect, great, good, bad, poor, totalnotes, maxcombo, minbp, "
        "playcount, clearcount, failcount, rank, rate, clear_db, clear_sd, clear_ex, "
        "op_best, rseed, ghost "
        "FROM score WHERE hash = ?";

    bool found = false;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, songHash.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            out.clear = sqlite3_column_int(stmt, 0);
            out.perfect = sqlite3_column_int(stmt, 1);
            out.great = sqlite3_column_int(stmt, 2);
            out.good = sqlite3_column_int(stmt, 3);
            out.bad = sqlite3_column_int(stmt, 4);
            out.poor = sqlite3_column_int(stmt, 5);
            out.totalnotes = sqlite3_column_int(stmt, 6);
            out.maxcombo = sqlite3_column_int(stmt, 7);
            out.minbp = sqlite3_column_int(stmt, 8);
            out.playcount = sqlite3_column_int(stmt, 9);
            out.clearcount = sqlite3_column_int(stmt, 10);
            out.failcount = sqlite3_column_int(stmt, 11);
            out.rank = sqlite3_column_int(stmt, 12);
            out.rate = sqlite3_column_int(stmt, 13);
            out.clear_db = sqlite3_column_int(stmt, 14);
            out.clear_sd = sqlite3_column_int(stmt, 15);
            out.clear_ex = sqlite3_column_int(stmt, 16);
            out.op_best = sqlite3_column_int(stmt, 17);
            out.rseed = sqlite3_column_int(stmt, 18);
            if (sqlite3_column_type(stmt, 19) != SQLITE_NULL) {
                const unsigned char* ghostText = sqlite3_column_text(stmt, 19);
                const int ghostBytes = sqlite3_column_bytes(stmt, 19);
                if (ghostText != nullptr && ghostBytes > 0) {
                    out.ghost.assign(reinterpret_cast<const char*>(ghostText), static_cast<std::size_t>(ghostBytes));
                }
            }
            found = true;
        }
        sqlite3_finalize(stmt);
    } else {
        DebugLog(
            "WARN",
            "db_query",
            std::format("result=prepare_fail sqlite={} hash8={}", sqlite3_errmsg(db), HashPrefix8(songHash))
        );
    }

    sqlite3_close(db);
    DebugLog("INFO", "db_query", std::format("result={} hash8={}", found ? "ok" : "no_row", HashPrefix8(songHash)));
    return found;
}

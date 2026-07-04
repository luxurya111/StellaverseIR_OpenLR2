#include "rank.h"

#include "constants.h"
#include "http.h"
#include "http_auth.h"
#include "json_util.h"
#include "log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CachedChartStats {
    int playerCount{};
    std::array<int, 6> clearDistribution{};
};

struct BoardCacheEntry {
    int rank{};
    int playerId{};
    int exscore{};
    int clearType{};
    int pgreat{};
    int great{};
    int good{};
    int bad{};
    int poor{};
    int maxcombo{};
    int minbp{};
    int playcount{};
    int optionPacked{};
    int keymode{};
    std::string displayName;
};

struct BoardCacheLeaderboard {
    int playerCount{};
    int myRank{};
    int myPlayerId{};
    std::vector<BoardCacheEntry> entries;
};

std::string UrlEncodeQueryComponent(std::string_view value) {
    std::string out;
    out.reserve(value.size() * 3);
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out += static_cast<char>(ch);
        } else {
            out += std::format("%{:02X}", ch);
        }
    }
    return out;
}

std::string RankCacheStorageKey(std::string_view hash) {
    if (hash.size() <= 50) {
        std::string key(hash);
        for (char& ch : key) {
            if (!std::isalnum(static_cast<unsigned char>(ch))) {
                ch = '_';
            }
        }
        return key;
    }
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const unsigned char byte : hash) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    }
    return std::format("{:08x}", ~crc);
}

std::filesystem::path RankCacheDirectory() {
    return GetModuleDirectory() / "IR";
}

bool ReadFreshCacheFile(const std::filesystem::path& path, json& out) {
    if (!ReadJsonFile(path, out)) {
        return false;
    }
    const auto fetchedAt = JsonField<std::int64_t>(out, {"fetched_at"});
    if (!fetchedAt) {
        return false;
    }
    const std::int64_t age = UnixTimeNow() - *fetchedAt;
    return age >= 0 && age < kRankCacheTtlSeconds;
}

void ParseBoardSnapshotBody(
    const json& body,
    openlr2::IRRankResult& rank,
    CachedChartStats& stats,
    BoardCacheLeaderboard& board
) {
    rank.myRank = JsonFieldOr<int>(body, {"rank", "my_rank", "myRank"}, rank.myRank);
    rank.totalPlayer = JsonFieldOr<int>(body, {"player_count", "playerCount", "total_player", "totalPlayer"}, rank.totalPlayer);
    rank.totalPlaycount = JsonFieldOr<int>(body, {"total_playcount", "totalPlaycount"}, rank.totalPlaycount);

    stats.playerCount = JsonFieldOr<int>(body, {"player_count", "playerCount"}, stats.playerCount);
    if (const auto it = body.find("clear_distribution"); it != body.end() && it->is_array() && it->size() >= 6) {
        for (std::size_t i = 0; i < 6; ++i) {
            stats.clearDistribution[i] = (*it)[i].get<int>();
        }
    }

    board.playerCount = JsonFieldOr<int>(body, {"player_count", "playerCount"}, board.playerCount);
    board.myRank = JsonFieldOr<int>(body, {"my_rank", "myRank"}, board.myRank);
    board.myPlayerId = JsonFieldOr<int>(body, {"my_player_id", "myPlayerId"}, board.myPlayerId);
    if (const auto entriesIt = body.find("entries"); entriesIt != body.end() && entriesIt->is_array()) {
        for (const auto& item : *entriesIt) {
            if (!item.is_object()) {
                continue;
            }
            board.entries.push_back({
                .rank = JsonFieldOr<int>(item, {"rank"}, 0),
                .playerId = JsonFieldOr<int>(item, {"player_id", "playerId"}, 0),
                .exscore = JsonFieldOr<int>(item, {"exscore"}, 0),
                .clearType = JsonFieldOr<int>(item, {"clear_type", "clearType"}, 0),
                .pgreat = JsonFieldOr<int>(item, {"pgreat"}, 0),
                .great = JsonFieldOr<int>(item, {"great"}, 0),
                .good = JsonFieldOr<int>(item, {"good"}, 0),
                .bad = JsonFieldOr<int>(item, {"bad"}, 0),
                .poor = JsonFieldOr<int>(item, {"poor"}, 0),
                .maxcombo = JsonFieldOr<int>(item, {"maxcombo"}, 0),
                .minbp = JsonFieldOr<int>(item, {"minbp"}, 0),
                .playcount = JsonFieldOr<int>(item, {"playcount"}, 0),
                .optionPacked = JsonFieldOr<int>(item, {"option_packed", "optionPacked"}, 0),
                .keymode = JsonFieldOr<int>(item, {"keymode"}, 0),
                .displayName = JsonFieldOr<std::string>(item, {"display_name", "displayName"}, {}),
            });
        }
    }

    if (board.myRank > 0) {
        rank.myRank = board.myRank;
    } else if (rank.myRank > 0) {
        board.myRank = rank.myRank;
    }
    const int playerCount = std::max({rank.totalPlayer, stats.playerCount, board.playerCount});
    if (playerCount > 0) {
        rank.totalPlayer = playerCount;
        stats.playerCount = playerCount;
        board.playerCount = playerCount;
    }
    rank.clearPlayers = stats.clearDistribution;
    rank.ranking.clear();
    rank.ranking.reserve(board.entries.size());
    for (const auto& entry : board.entries) {
        openlr2::IRRankPlayer player{};
        player.name = entry.displayName;
        player.id = entry.playerId;
        if (entry.clearType >= 0 && entry.clearType <= static_cast<int>(openlr2::Lamp::FullCombo)) {
            player.clear = static_cast<openlr2::Lamp>(entry.clearType);
        }
        player.maxcombo = entry.maxcombo;
        player.pg = entry.pgreat;
        player.gr = entry.great;
        player.gd = entry.good;
        player.bd = entry.bad;
        player.pr = entry.poor;
        player.minbp = entry.minbp;
        player.playcount = entry.playcount;
        rank.ranking.push_back(std::move(player));
    }
    if (const auto fetchedAt = JsonField<std::int64_t>(body, {"fetched_at"})) {
        rank.lastupdate = static_cast<uint64_t>(*fetchedAt);
    }
}

void WriteBoardCachesFromSnapshot(
    std::string_view songHash,
    const openlr2::IRRankResult& rank,
    const CachedChartStats& stats,
    const BoardCacheLeaderboard& board
) {
    std::error_code ec;
    std::filesystem::create_directories(RankCacheDirectory(), ec);

    json body = {
        {"hash", std::string(songHash)},
        {"fetched_at", UnixTimeNow()},
        {"player_count", rank.totalPlayer},
        {"rank", rank.myRank},
        {"my_rank", rank.myRank},
        {"clear_distribution", stats.clearDistribution},
        {"entries", json::array()},
    };
    if (board.myPlayerId > 0) {
        body["my_player_id"] = board.myPlayerId;
    }
    if (rank.totalPlaycount > 0) {
        body["total_playcount"] = rank.totalPlaycount;
    }
    for (const auto& entry : board.entries) {
        body["entries"].push_back({
            {"rank", entry.rank},
            {"player_id", entry.playerId},
            {"display_name", entry.displayName},
            {"exscore", entry.exscore},
            {"clear_type", entry.clearType},
            {"pgreat", entry.pgreat},
            {"great", entry.great},
            {"good", entry.good},
            {"bad", entry.bad},
            {"poor", entry.poor},
            {"maxcombo", entry.maxcombo},
            {"minbp", entry.minbp},
            {"playcount", entry.playcount},
            {"option_packed", entry.optionPacked},
            {"keymode", entry.keymode},
        });
    }
    if (!WriteJsonFile(RankCacheDirectory() / (RankCacheStorageKey(songHash) + ".json"), body)) {
        DebugLog("WARN", "rank_cache_write_fail", std::format("hash8={}", HashPrefix8(songHash)));
    }
}

} // namespace

void InvalidateChartRankCaches(std::string_view hash) {
    if (hash.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(
        RankCacheDirectory() / (RankCacheStorageKey(hash) + ".json"),
        ec
    );
}

bool TryReadRankCache(std::string_view hash, openlr2::IRRankResult& out) {
    json body;
    const auto path = RankCacheDirectory() / (RankCacheStorageKey(hash) + ".json");
    if (!ReadFreshCacheFile(path, body)) {
        return false;
    }
    openlr2::IRRankResult parsed{};
    CachedChartStats stats{};
    BoardCacheLeaderboard board{};
    ParseBoardSnapshotBody(body, parsed, stats, board);
    out = std::move(parsed);
    return true;
}

HttpStatus GetRankFromNetwork(
    std::string_view songHash,
    const std::string& apiKey,
    openlr2::IRRankResult& out
) {
    const std::string queryString = std::format(
        "hash={}&limit=999&offset=0",
        UrlEncodeQueryComponent(songHash)
    );
    const HttpAuthEndpoint boardEndpoint =
        songHash.size() >= 64 ? HttpAuthEndpoint::CourseBoard : HttpAuthEndpoint::ChartBoard;

    return FetchWithRetry([&]() {
        out = {};
        std::string body;
        const HttpStatus status = HttpGetJson(queryString, apiKey, boardEndpoint, "board_fetch", body);
        if (status != HttpStatus::Ok) {
            return status;
        }

        const json parsedBody = json::parse(body, nullptr, false);
        if (parsedBody.is_discarded()) {
            DebugLog("WARN", "board_parse_fail", std::format("body_size={}", body.size()));
            return HttpStatus::Fail;
        }

        openlr2::IRRankResult rank{};
        CachedChartStats stats{};
        BoardCacheLeaderboard board{};
        ParseBoardSnapshotBody(parsedBody, rank, stats, board);
        out = rank;
        if (out.lastupdate == 0) {
            out.lastupdate = static_cast<uint64_t>(UnixTimeNow());
        }
        WriteBoardCachesFromSnapshot(songHash, rank, stats, board);
        return HttpStatus::Ok;
    });
}

HttpStatus GetGhostFromNetwork(
    std::string_view songHash,
    openlr2::GhostMode mode,
    int targetPlayerId,
    const std::string& apiKey,
    openlr2::IRGhostResult& out
) {
    const char* modeValue = "target";
    switch (mode) {
    case openlr2::GhostMode::Top: modeValue = "top"; break;
    case openlr2::GhostMode::Next: modeValue = "next"; break;
    case openlr2::GhostMode::Average: modeValue = "average"; break;
    default: break;
    }
    const std::string queryString = std::format(
        "hash={}&target_player_id={}&mode={}",
        UrlEncodeQueryComponent(songHash),
        targetPlayerId,
        modeValue
    );

    return FetchWithRetry([&]() {
        std::string body;
        const HttpStatus status = HttpGetJson(queryString, apiKey, HttpAuthEndpoint::ChartGhost, "ghost_fetch", body);
        if (status != HttpStatus::Ok) {
            return status;
        }

        out = {};
        const json j = json::parse(body, nullptr, false);
        if (j.is_discarded()) {
            return status;
        }
        out.displayName = JsonFieldOr<std::string>(j, {"display_name", "displayName"}, {});
        out.ghostData = JsonFieldOr<std::string>(j, {"ghost_data", "ghostData"}, {});
        const int packed = JsonFieldOr<int>(j, {"option_packed", "optionPacked"}, 0);
        const auto digit = [packed](int pos) {
            if (packed <= 0 || pos <= 0) {
                return 0;
            }
            int value = packed;
            for (int i = 1; i < pos; ++i) {
                value /= 10;
            }
            return value % 10;
        };
        const auto gauge = [](int d) {
            switch (d) {
            case 0: return openlr2::Gauge::Groove;
            case 1: return openlr2::Gauge::Survival;
            case 2: return openlr2::Gauge::Death;
            case 3: return openlr2::Gauge::Easy;
            case 4: return openlr2::Gauge::PAttack;
            case 5: return openlr2::Gauge::GAttack;
            default: return openlr2::Gauge::Unknown;
            }
        };
        const auto random = [](int d) {
            switch (d) {
            case 0: return openlr2::Random::No;
            case 1: return openlr2::Random::Mirror;
            case 2: return openlr2::Random::Random;
            case 3: return openlr2::Random::SRandom;
            case 4: return openlr2::Random::Scatter;
            case 5: return openlr2::Random::Converge;
            default: return openlr2::Random::No;
            }
        };
        out.gauge = gauge(digit(1));
        out.randomOption[0] = random(digit(2));
        out.randomOption[1] = random(digit(3));
        out.dpflip = digit(4) != 0;
        out.rseed = JsonFieldOr<int>(j, {"random_seed", "randomSeed"}, -1);
        out.averageExscore = JsonFieldOr<int>(j, {"average_exscore", "averageExscore"}, 0);
        return status;
    });
}

#include "rival.h"

#include "constants.h"
#include "http.h"
#include "http_auth.h"
#include "json_util.h"
#include "log.h"

#include <filesystem>
#include <format>
#include <string>

std::filesystem::path RivalDirectory() {
    return GetModuleDirectory() / "Rival";
}

bool FillRivalScoresFromJson(const json& body, std::vector<openlr2::IRRivalScore>& out) {
    out.clear();
    if (const auto it = body.find("scores"); it != body.end() && it->is_array()) {
        for (const auto& row : *it) {
            const auto hash = JsonFieldOr<std::string>(row, {"hash"}, {});
            if (hash.empty()) {
                continue;
            }
            openlr2::IRRivalScore score{};
            score.hash = hash;
            score.clear = JsonFieldOr<int>(row, {"clear"}, 0);
            score.notes = JsonFieldOr<int>(row, {"notes"}, 0);
            score.combo = JsonFieldOr<int>(row, {"combo"}, 0);
            score.pg = JsonFieldOr<int>(row, {"pg"}, 0);
            score.gr = JsonFieldOr<int>(row, {"gr"}, 0);
            score.gd = JsonFieldOr<int>(row, {"gd"}, 0);
            score.bd = JsonFieldOr<int>(row, {"bd"}, 0);
            score.pr = JsonFieldOr<int>(row, {"pr"}, 0);
            score.minbp = JsonFieldOr<int>(row, {"minbp"}, 0);
            score.option = JsonFieldOr<int>(row, {"option"}, 0);
            score.lastupdate = JsonFieldOr<std::uint64_t>(row, {"lastupdate"}, 0);
            out.push_back(std::move(score));
        }
    }
    return true;
}

bool WriteRivalScoreJsonCache(int rivalId, const json& scoreBody) {
    const auto rivalDir = RivalDirectory();
    std::error_code ec;
    std::filesystem::create_directories(rivalDir, ec);
    if (!WriteJsonFile(rivalDir / std::format("{}.json", rivalId), scoreBody)) {
        DebugLog("WARN", "rival_scores_cache_write_fail", std::format("id={}", rivalId));
        return false;
    }
    return true;
}

bool ReadRivalScoreJsonCache(int rivalId, json& out) {
    return ReadJsonFile(RivalDirectory() / std::format("{}.json", rivalId), out);
}

HttpStatus FetchRivalScore(int rivalId, json& out) {
    out = {};
    const std::string apiKey = LoadApiKey();
    if (apiKey.empty()) {
        DebugLog("WARN", "rival_score_fetch_no_api_key", std::format("id={}", rivalId));
        return HttpStatus::Fail;
    }

    std::string body;
    const std::string query = std::format("rival_id={}", rivalId);
    const HttpStatus status = FetchWithRetry([&]() {
        body.clear();
        const HttpStatus httpStatus = HttpGetJson(query, apiKey, HttpAuthEndpoint::IrRival, "rival_score_fetch", body);
        if (httpStatus != HttpStatus::Ok) {
            return httpStatus;
        }
        const json parsed = json::parse(body, nullptr, false);
        if (parsed.is_discarded() || !parsed.contains("scores") || !parsed["scores"].is_array()) {
            DebugLog("WARN", "rival_score_fetch_bad_json", std::format("id={}", rivalId));
            return HttpStatus::Fail;
        }
        out = parsed;
        return HttpStatus::Ok;
    });
    return status;
}

json BuildRivalListCache(const json& snapshot) {
    json body = {
        {"fetched_at", JsonFieldOr<std::int64_t>(snapshot, {"fetched_at"}, UnixTimeNow())},
        {"player_id", JsonFieldOr<int>(snapshot, {"player_id"}, 0)},
        {"rivals", json::array()},
    };
    if (const auto it = snapshot.find("rivals"); it != snapshot.end() && it->is_array()) {
        for (const auto& rival : *it) {
            const int id = JsonFieldOr<int>(rival, {"id"}, 0);
            if (id <= 0) {
                continue;
            }
            body["rivals"].push_back({
                {"id", id},
                {"name", JsonFieldOr<std::string>(rival, {"name"}, std::to_string(id))},
                {"lastupdate", JsonFieldOr<int>(rival, {"lastupdate"}, 0)},
            });
        }
    }
    return body;
}

bool WriteRivalListCache(const json& listBody) {
    const auto rivalDir = RivalDirectory();
    std::error_code ec;
    std::filesystem::create_directories(rivalDir, ec);

    const json listCache = BuildRivalListCache(listBody);
    if (!WriteJsonFile(rivalDir / "rivals.json", listCache)) {
        DebugLog("WARN", "rival_list_cache_write_fail", {});
        return false;
    }
    return true;
}

HttpStatus FetchRivalList(json& out) {
    out = {};
    const std::string apiKey = LoadApiKey();
    if (apiKey.empty()) {
        DebugLog("WARN", "rival_list_fetch_no_api_key", {});
        return HttpStatus::Fail;
    }

    std::string body;
    const HttpStatus status = FetchWithRetry([&]() {
        body.clear();
        const HttpStatus httpStatus = HttpGetJson("", apiKey, HttpAuthEndpoint::IrRivals, "rival_list_fetch", body);
        if (httpStatus != HttpStatus::Ok) {
            return httpStatus;
        }
        const json parsed = json::parse(body, nullptr, false);
        if (parsed.is_discarded() || !parsed.contains("rivals") || !parsed["rivals"].is_array()) {
            DebugLog("WARN", "rival_list_fetch_bad_json", {});
            return HttpStatus::Fail;
        }
        out = parsed;
        return HttpStatus::Ok;
    });
    return status;
}

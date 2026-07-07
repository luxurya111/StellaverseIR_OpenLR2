#include <LR2_customir_api.h>

#include "http.h"
#include "http_auth.h"
#include "json_util.h"
#include "log.h"
#include "rank.h"
#include "score_db.h"
#include "score_payload.h"

#include <format>
#include <string>

static const char* GetName() {
    return "StellaverseIR";
}

static openlr2::GetStatus GetGhost(
    const char* songHash,
    openlr2::GhostMode mode,
    int targetPlayerId,
    openlr2::IRGhostResult& out
) {
    out = {};
    if (songHash == nullptr || songHash[0] == '\0') {
        return openlr2::GetStatus::Fail;
    }
    const std::string apiKey = LoadApiKey();
    if (apiKey.empty()) {
        return openlr2::GetStatus::Fail;
    }
    openlr2::IRGhostResult parsed{};
    switch (GetGhostFromNetwork(songHash, mode, targetPlayerId, apiKey, parsed)) {
    case HttpStatus::Ok:
        out = std::move(parsed);
        return openlr2::GetStatus::Ok;
    case HttpStatus::Retry:
        return openlr2::GetStatus::Retry;
    default:
        return openlr2::GetStatus::Fail;
    }
}

static bool Login() {
    ResetDebugLogOnce();

    const std::string apiKey = LoadApiKey();
    if (apiKey.empty()) {
        DebugLog("WARN", "login_skip_no_api_key", {});
        return false;
    }

    const HttpStatus status = FetchWithRetry([&]() {
        std::string body;
        const HttpStatus httpStatus = HttpGetJson("", apiKey, HttpAuthEndpoint::IrLogin, "login_fetch", body);
        if (httpStatus != HttpStatus::Ok) {
            return httpStatus;
        }
        const json parsed = json::parse(body, nullptr, false);
        if (parsed.is_discarded() || !JsonFieldOr<bool>(parsed, {"logged_in"}, false)) {
            return HttpStatus::Fail;
        }
        return HttpStatus::Ok;
    });

    const bool loggedIn = status == HttpStatus::Ok;
    DebugLog("INFO", "login_done", std::format("logged_in={}", loggedIn ? 1 : 0));
    return loggedIn;
}

static SendScoreStatus SendScore(const IRScoreV1& score) {
    const int player = score.state.player;
    if (player >= 0 && player < static_cast<int>(score.settings.assist.size())
        && score.settings.assist[static_cast<std::size_t>(player)] != 0) {
        DebugLog("INFO", "skip_assist", {});
        return SendScoreStatus::Ok;
    }
    if (score.state.isNosave != 0) {
        DebugLog("INFO", "skip_nosave", {});
        return SendScoreStatus::Ok;
    }

    const std::string apiKey = LoadApiKey();
    if (apiKey.empty()) {
        DebugLog("WARN", "skip_no_api_key", {});
        return SendScoreStatus::Fail;
    }
    if (score.song.hash.empty()) {
        DebugLog("WARN", "skip_invalid_hash", {});
        return SendScoreStatus::Fail;
    }

    LocalScoreRow dbRow{};
    const bool hasDbRow = QueryLocalScore(score.song.hash, dbRow);
    const ResolvedGhostForPost ghostForPost = ResolveGhostDataForPost(
        score.ghostData,
        hasDbRow,
        hasDbRow ? &dbRow : nullptr
    );
    const bool isPb = DetectPersonalBest(score, hasDbRow ? &dbRow : nullptr, hasDbRow);
    const LocalScoreRow* previousBest = (!isPb && hasDbRow) ? &dbRow : nullptr;

    const std::string body = BuildScoreJson(
        score,
        ghostForPost.data,
        isPb,
        hasDbRow,
        hasDbRow ? &dbRow : nullptr,
        previousBest
    );

    const int courseType = score.song.courseType;
    const HttpAuthEndpoint scoreEndpoint =
        (score.song.courseStageCount > 0 && (courseType == 0 || courseType == 2))
            ? HttpAuthEndpoint::CourseScore
            : HttpAuthEndpoint::ChartScore;
    const SendScoreStatus result = PostScoreJson(body, apiKey, scoreEndpoint);
    if (result == SendScoreStatus::Ok) {
        InvalidateChartRankCaches(score.song.hash);
    } else {
        DebugLog("WARN", "send_score_fail", std::format("hash8={}", HashPrefix8(score.song.hash)));
    }
    return result;
}

static openlr2::GetStatus GetResultRank(const char* songHash, int reserved, openlr2::IRRankResult& out) {
    (void)reserved;
    out = {};
    if (songHash == nullptr || songHash[0] == '\0') {
        return openlr2::GetStatus::Fail;
    }
    const std::string apiKey = LoadApiKey();
    if (apiKey.empty()) {
        return openlr2::GetStatus::Fail;
    }
    switch (GetRankFromNetwork(songHash, apiKey, out)) {
    case HttpStatus::Ok: return openlr2::GetStatus::Ok;
    case HttpStatus::Retry: return openlr2::GetStatus::Retry;
    default: return openlr2::GetStatus::Fail;
    }
}

static openlr2::GetStatus RestoreCachedRank(const char* songHash, int reserved, openlr2::IRRankResult& out) {
    (void)reserved;
    out = {};
    if (songHash == nullptr || songHash[0] == '\0') {
        return openlr2::GetStatus::Ok;
    }
    if (TryReadRankCache(songHash, out)) {
        return openlr2::GetStatus::Ok;
    }
    return openlr2::GetStatus::Fail;
}

static std::string GetWebRankingUrl(const char* songHash) {
	return std::format("{}{}", HttpAuth_WebRankingUrlBase(), songHash);
}

// F5 / IR button: older OpenLR2 reads webRankingUrlTemplate and replaces `{hash}`.
// Host also requires display IR login (IsDisplayIrOnline) before opening the page.
extern "C" OLR2_IR_EXPORT void OLR2_IR_API GetMethodTable(MethodTable& table) {
    table.GetName = &GetName;
    table.LoginV1 = &Login;
    table.SendScoreV1 = &SendScore;
    table.GetResultRank = &GetResultRank;
    table.RestoreCachedRank = &RestoreCachedRank;
    table.GetGhost = &GetGhost;
    table.webRankingUrlTemplate = HttpAuth_WebRankingUrlTemplate();
    table.GetWebRankingUrl = &GetWebRankingUrl;
}

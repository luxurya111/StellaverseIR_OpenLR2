#include "score_payload.h"

#include "constants.h"
#include "json_util.h"

#include <iterator>

namespace {

json PreviousBestToJson(const LocalScoreRow& row, int keymode) {
    const int gauge = row.op_best % 10;
    const int random0 = (row.op_best / 10) % 10;
    const char* lamp = (row.clear >= 0 && row.clear < static_cast<int>(std::size(kLamps)))
        ? kLamps[row.clear]
        : "UNKNOWN";
    json previousBest = {
        {"option_1", gauge},
        {"option_2", random0},
        {"rseed", row.rseed},
        {"exscore", row.great + row.perfect * 2},
        {"playcount", row.playcount},
        {"clear", row.clear},
        {"pgreat", row.perfect},
        {"great", row.great},
        {"good", row.good},
        {"bad", row.bad},
        {"poor", row.poor},
        {"maxcombo", row.maxcombo},
        {"totalnotes", row.totalnotes},
        {"rank", row.rank},
        {"rate", row.rate},
        {"clear_db", row.clear_db},
        {"clear_sd", row.clear_sd},
        {"clear_ex", row.clear_ex},
        {"clearcount", row.clearcount},
        {"failcount", row.failcount},
        {"minbp", row.minbp},
        {"lamp", lamp},
    };
    if (keymode >= 10) {
        previousBest["random_2"] = (row.op_best / 100) % 10;
        previousBest["dpflip"] = (row.op_best / 1000) % 10;
    }
    return previousBest;
}

} // namespace

std::string BuildScoreJson(
    const IRScoreV1& score,
    std::string_view ghostDataForPost,
    bool isPb,
    bool hasDbRow,
    const LocalScoreRow* dbRow,
    const LocalScoreRow* previousBest
) {
    const auto& j = score.judgements_total;
    const auto& song = score.song;
    const auto& settings = score.settings;
    const auto& state = score.state;
    const char* lamp = (score.clearType >= 0 && score.clearType < static_cast<int>(std::size(kLamps)))
        ? kLamps[score.clearType]
        : "UNKNOWN";

    json body = {
        {"isPB", isPb},
        {"song", {
            {"hash", song.hash},
            {"title", song.title},
            {"subtitle", song.subtitle},
            {"genre", song.genre},
            {"artist", song.artist},
            {"subartist", song.subartist},
            {"maxBPM", song.maxBPM},
            {"minBPM", song.minBPM},
            {"longnote", song.longnote},
            {"random", song.random},
            {"judge", song.judge},
            {"level", score.songPlayLevel},
            {"courseStageCount", song.courseStageCount},
            {"courseType", song.courseType},
        }},
        {"settings", {
            {"gaugeOption", settings.gaugeOption},
            {"random", settings.random},
            {"autokey", settings.autokey},
            {"assist", settings.assist},
            {"dpflip", settings.dpflip},
            {"hsfix", settings.hsfix},
            {"randSC", settings.randSC},
            {"randFix", settings.randFix},
            {"m_softlanding", settings.m_softlanding},
            {"m_addmine", settings.m_addmine},
            {"m_addlong", settings.m_addlong},
            {"m_earthquake", settings.m_earthquake},
            {"m_tornado", settings.m_tornado},
            {"m_superloop", settings.m_superloop},
            {"m_gambol", settings.m_gambol},
            {"m_char", settings.m_char},
            {"m_heartbeat", settings.m_heartbeat},
            {"m_loudness", settings.m_loudness},
            {"m_addnote", settings.m_addnote},
            {"m_nabeatsu", settings.m_nabeatsu},
            {"m_accel", settings.m_accel},
            {"m_sincurve", settings.m_sincurve},
            {"m_wave", settings.m_wave},
            {"m_spiral", settings.m_spiral},
            {"m_sidejump", settings.m_sidejump},
            {"is_extra", settings.is_extra},
            {"m_extra", settings.m_extra},
            {"m_lunaris", static_cast<unsigned char>(settings.m_lunaris)},
            {"m_gas", settings.m_gas},
            {"gomiscore", settings.gomiscore},
            {"disablecurspeedchange", settings.disablecurspeedchange},
        }},
        {"state", {
            {"player", state.player},
            {"keymode", state.keymode},
            {"randomseed", state.randomseed},
            {"freqSpeedMultiplier", state.freqSpeedMultiplier},
            {"song_runtime", state.song_runtime},
            {"isNosave", static_cast<unsigned char>(state.isNosave)},
            {"isForceEasy", static_cast<unsigned char>(state.isForceEasy)},
            {"isCourse", static_cast<unsigned char>(state.isCourse)},
            {"courseStageNow", state.courseStageNow},
            {"notes_total", state.notes_total},
        }},
        {"judgements_total", {
            {"epg", j.epg}, {"lpg", j.lpg}, {"egr", j.egr}, {"lgr", j.lgr},
            {"egd", j.egd}, {"lgd", j.lgd}, {"ebd", j.ebd}, {"lbd", j.lbd},
            {"epr", j.epr}, {"lpr", j.lpr}, {"cb", j.cb},
            {"fast", j.fast}, {"slow", j.slow},
            {"notes_played", j.notes_played}, {"notes_total", j.notes_total},
        }},
        {"max_combo", score.max_combo},
        {"HP", score.HP},
        {"gaugeType", score.gaugeType},
        {"moneyscore", score.moneyscore},
        {"exscore", score.exscore},
        {"rate", score.rate},
        {"clearType", score.clearType},
        {"lamp", lamp},
        {"inputType", score.inputType},
    };

    if (isPb) {
        body["previous_best"] = (hasDbRow && dbRow != nullptr)
            ? PreviousBestToJson(*dbRow, score.state.keymode)
            : json(nullptr);
    } else if (previousBest != nullptr) {
        body["previous_best"] = PreviousBestToJson(*previousBest, score.state.keymode);
    } else {
        body["previous_best"] = nullptr;
    }

    if (!ghostDataForPost.empty()) {
        body["ghost_data"] = std::string(ghostDataForPost);
    }

    return body.dump();
}

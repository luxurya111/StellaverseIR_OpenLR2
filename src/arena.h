#pragma once

#include <LR2_customir_api.h>

// Stellaverse Arena transport (MethodTable Arena* callbacks).
//
// HTTP (HMAC-signed, same plane as /api/chart/score):
//   POST /api/arena/rooms/{id}/join | leave | result, GET /api/arena/active
// Live play: WinHTTP WebSocket to the returned wsUrl (+ token), frames are
//   [1 byte id][UTF-8 JSON] (see stellaverse-ir/docs/arena-protocol.md).
//
// All functions are thread-safe. Implementation is Windows-only (WinHTTP); on
// other platforms every call is a no-op / Fail.

namespace stellaverse_arena {

openlr2::GetStatus OLR2_IR_API JoinRoom(const char* roomId, openlr2::ArenaJoinResult& out);
openlr2::GetStatus OLR2_IR_API JoinActiveRoom(openlr2::ArenaJoinResult& out);
void OLR2_IR_API Leave();
bool OLR2_IR_API IsConnected();
void OLR2_IR_API PollState(openlr2::ArenaRoomState& out);

void OLR2_IR_API SendSelectedChart(
    int randomSeed,
    const char* hash,
    const char* title,
    const char* artist,
    unsigned option,
    unsigned gauge,
    bool itemModeEnabled
);
// Non-final snapshots are throttled to 10 Hz; `finalScore` always goes out.
void OLR2_IR_API SendScore(const openlr2::ArenaPeerScore& score, bool finalScore);
void OLR2_IR_API SendReady();
void OLR2_IR_API SendCancelled();
void OLR2_IR_API SendChat(const char* message);
void OLR2_IR_API SendMissingChart();
void OLR2_IR_API SendItemJson(const char* jsonObject);

openlr2::GetStatus OLR2_IR_API SubmitRoomResult(const char* roomId, const char* chartHash, const char* resultJson);

} // namespace stellaverse_arena

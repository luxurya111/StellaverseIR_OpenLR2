#pragma once

#include <string>
#include <string_view>

enum class HttpAuthEndpoint {
    ChartScore,
    CourseScore,
    ChartBoard,
    CourseBoard,
    ChartGhost,
    IrLogin,
    IrRivals,
    IrRival,
    // Arena. CreateRoom/Active have fixed paths; Join/Leave/Result are per-room
    // (/api/arena/rooms/{roomId}/...) and must be resolved via HttpAuth_ArenaPath().
    ArenaCreateRoom,
    ArenaActive,
    ArenaJoinRoom,
    ArenaLeave,
    ArenaResult,
};

// Builds WinHTTP request headers including authentication.
// `payload` is the POST body or GET query string (without '?').
// Returns an empty string on failure.
std::wstring HttpAuth_BuildHeaders(
    std::string_view apiKey,
    std::string_view payload
);

std::wstring HttpAuth_UserAgent();

const wchar_t* HttpAuth_Host();
unsigned short HttpAuth_Port();
unsigned long HttpAuth_RequestFlags();
// Fixed path for an endpoint. Returns L"" for per-room Arena endpoints
// (use HttpAuth_ArenaPath for those).
const wchar_t* HttpAuth_Path(HttpAuthEndpoint endpoint);

// Resolves any Arena endpoint to a request path. `roomId` is percent-encoded
// and only used by ArenaJoinRoom / ArenaLeave / ArenaResult.
// Returns an empty string for non-Arena endpoints or when a required roomId is empty.
std::wstring HttpAuth_ArenaPath(HttpAuthEndpoint endpoint, std::string_view roomId);

const char* HttpAuth_WebRankingUrlBase();

// Stable pointer for MethodTable (static storage).
const char* HttpAuth_WebRankingUrlTemplate();

// Short label for debug logs only (not a URL path).
const char* HttpAuth_EndpointTag(HttpAuthEndpoint endpoint);

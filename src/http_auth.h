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
const wchar_t* HttpAuth_Path(HttpAuthEndpoint endpoint);

// Stable pointer for MethodTable (static storage).
const char* HttpAuth_WebRankingUrlTemplate();

// Short label for debug logs only (not a URL path).
const char* HttpAuth_EndpointTag(HttpAuthEndpoint endpoint);

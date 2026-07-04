// Sample http_auth implementation (NOT used by the build automatically).
//
//   mkdir http_auth
//   copy examples\http_auth.sample.cpp http_auth\http_auth.cpp
//   // then implement your own host / paths / auth
//
// This sample only shows *where* to put configuration and request signing.
// It does not implement a real IR protocol.

#include "http_auth.h"

#include <string>

std::wstring HttpAuth_UserAgent() {
    return L"SampleIR/0.0.0";
}

// Called for every HTTP request. Put your authentication here
// (API key, signature, timestamps, custom headers, etc.).
//
// `payload` is the POST body or the GET query string (without '?').
std::wstring HttpAuth_BuildHeaders(std::string_view apiKey, std::string_view /*payload*/) {
    if (apiKey.empty()) {
        return {};
    }
    // Minimal placeholder — replace with your own header format.
    return L"Content-Type: application/json\r\n"
           L"Authorization: Bearer <your-token-here>\r\n";
}

const wchar_t* HttpAuth_Host() {
    return L"example.com";
}

unsigned short HttpAuth_Port() {
    return 443;
}

unsigned long HttpAuth_RequestFlags() {
    return 0x00800000; // WINHTTP_FLAG_SECURE
}

const wchar_t* HttpAuth_Path(HttpAuthEndpoint endpoint) {
    switch (endpoint) {
    case HttpAuthEndpoint::ChartScore: return L"/score";
    case HttpAuthEndpoint::CourseScore: return L"/course-score";
    case HttpAuthEndpoint::ChartBoard: return L"/board";
    case HttpAuthEndpoint::CourseBoard: return L"/course-board";
    case HttpAuthEndpoint::ChartGhost: return L"/ghost";
    case HttpAuthEndpoint::IrLogin: return L"/login";
    }
    return L"/";
}

const char* HttpAuth_WebRankingUrlTemplate() {
    return "https://example.com/ranking/{hash}";
}

const char* HttpAuth_EndpointTag(HttpAuthEndpoint endpoint) {
    switch (endpoint) {
    case HttpAuthEndpoint::IrLogin: return "login";
    case HttpAuthEndpoint::CourseScore:
    case HttpAuthEndpoint::CourseBoard: return "course";
    default: return "chart";
    }
}

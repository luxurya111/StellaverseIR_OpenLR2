#include "http_auth.h"

// Public stub used when http_auth/ is not present.

std::wstring HttpAuth_UserAgent() {
    return {};
}

std::wstring HttpAuth_BuildHeaders(std::string_view /*apiKey*/, std::string_view /*payload*/) {
    return {};
}

const wchar_t* HttpAuth_Host() {
    return L"";
}

unsigned short HttpAuth_Port() {
    return 0;
}

unsigned long HttpAuth_RequestFlags() {
    return 0;
}

const wchar_t* HttpAuth_Path(HttpAuthEndpoint /*endpoint*/) {
    return L"";
}

const char* HttpAuth_WebRankingUrlTemplate() {
    return "";
}

const char* HttpAuth_EndpointTag(HttpAuthEndpoint /*endpoint*/) {
    return "unknown";
}

#pragma once

#include <LR2_customir_api.h>

#include "http_auth.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

enum class HttpStatus { Ok, Retry, Fail };

std::int64_t UnixTimeNow();

HttpStatus HttpGetJson(
    const std::string& queryString,
    const std::string& apiKey,
    HttpAuthEndpoint endpoint,
    std::string_view logStage,
    std::string& bodyOut
);

SendScoreStatus PostScoreJson(
    const std::string& body,
    const std::string& apiKey,
    HttpAuthEndpoint endpoint
);

// Generic HMAC-signed requests against an arbitrary path on the IR host (Arena).
// Signing matches HttpGetJson / PostScoreJson: payload is the query string (GET)
// or the raw body (POST). `path` must start with '/'. Response body (any status)
// is stored in `bodyOut`; the HTTP status code (0 on transport failure) in `statusOut`.
HttpStatus HttpGetJsonPath(
    const std::wstring& path,
    const std::string& queryString,
    const std::string& apiKey,
    std::string& bodyOut,
    unsigned long* statusOut = nullptr
);

HttpStatus HttpPostJsonPath(
    const std::wstring& path,
    const std::string& body,
    const std::string& apiKey,
    std::string& bodyOut,
    unsigned long* statusOut = nullptr
);

HttpStatus FetchWithRetry(const std::function<HttpStatus()>& attempt);

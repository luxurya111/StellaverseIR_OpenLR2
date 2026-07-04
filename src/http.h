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

HttpStatus FetchWithRetry(const std::function<HttpStatus()>& attempt);

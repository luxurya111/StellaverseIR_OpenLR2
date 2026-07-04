#include "http.h"

#include "http_auth.h"
#include "log.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <thread>

#include <windows.h>
#include <winhttp.h>

namespace {

std::string ReadHttpResponseBody(HINTERNET request, std::size_t maxBytes) {
    std::string body;
    body.reserve(4096);
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
        if (body.size() >= maxBytes) {
            break;
        }
        const DWORD toRead = static_cast<DWORD>(std::min<std::size_t>(available, maxBytes - body.size()));
        std::string chunk(toRead, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), toRead, &read) || read == 0) {
            break;
        }
        chunk.resize(read);
        body += chunk;
    }
    return body;
}

} // namespace

std::int64_t UnixTimeNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

SendScoreStatus PostScoreJson(const std::string& body, const std::string& apiKey, HttpAuthEndpoint endpoint) {
    const wchar_t* path = HttpAuth_Path(endpoint);
    DebugLog(
        "INFO",
        "http_begin",
        std::format(
            "endpoint={} port={} secure={} body_size={}",
            HttpAuth_EndpointTag(endpoint),
            static_cast<int>(HttpAuth_Port()),
            HttpAuth_RequestFlags() != 0 ? 1 : 0,
            body.size()
        )
    );

    const std::wstring userAgentWide = HttpAuth_UserAgent();
    if (userAgentWide.empty() || path == nullptr || path[0] == L'\0') {
        DebugLog("ERROR", "http_auth_user_agent_fail", {});
        return SendScoreStatus::Fail;
    }

    HINTERNET session = WinHttpOpen(
        userAgentWide.c_str(),
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (session == nullptr) {
        DebugLogWin32Error("winhttp_open_fail");
        return SendScoreStatus::Retry;
    }

    DWORD dwHttp2 = WINHTTP_PROTOCOL_FLAG_HTTP2;
    DWORD dwSslProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(session, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &dwHttp2, sizeof(dwHttp2));
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &dwSslProtocols, sizeof(dwSslProtocols));

    HINTERNET connect = WinHttpConnect(
        session,
        HttpAuth_Host(),
        static_cast<INTERNET_PORT>(HttpAuth_Port()),
        0
    );
    if (connect == nullptr) {
        DebugLogWin32Error("winhttp_connect_fail");
        WinHttpCloseHandle(session);
        return SendScoreStatus::Retry;
    }

    HINTERNET request = WinHttpOpenRequest(
        connect,
        L"POST",
        path,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        static_cast<DWORD>(HttpAuth_RequestFlags())
    );
    if (request == nullptr) {
        DebugLogWin32Error("winhttp_request_fail");
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return SendScoreStatus::Retry;
    }
    DWORD dwDecompression = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(request, WINHTTP_OPTION_DECOMPRESSION, &dwDecompression, sizeof(dwDecompression));

    const std::wstring headers = HttpAuth_BuildHeaders(apiKey, body);
    if (headers.empty()) {
        DebugLog("ERROR", "http_auth_headers_fail", {});
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return SendScoreStatus::Fail;
    }

    const DWORD bodySize = static_cast<DWORD>(body.size());
    const BOOL sent = WinHttpSendRequest(
        request,
        headers.c_str(),
        static_cast<DWORD>(-1),
        bodySize == 0 ? WINHTTP_NO_REQUEST_DATA : const_cast<LPVOID>(static_cast<LPCVOID>(body.data())),
        bodySize,
        bodySize,
        0
    );
    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        DebugLog("ERROR", "winhttp_send_fail", std::format("sent={}", sent ? 1 : 0));
        DebugLogWin32Error("winhttp_send_fail_err");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return SendScoreStatus::Retry;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX
    );

    const SendScoreStatus mapped = statusCode >= 200 && statusCode < 300 ? SendScoreStatus::Ok
        : statusCode >= 400 && statusCode < 500 ? SendScoreStatus::Fail
        : SendScoreStatus::Retry;
    const char* mappedName = mapped == SendScoreStatus::Ok ? "Ok"
        : mapped == SendScoreStatus::Retry ? "Retry" : "Fail";
    DebugLog("INFO", "http_done", std::format("status_code={} mapped_status={}", statusCode, mappedName));

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return mapped;
}

HttpStatus HttpGetJson(
    const std::string& queryString,
    const std::string& apiKey,
    HttpAuthEndpoint endpoint,
    std::string_view logStage,
    std::string& bodyOut
) {
    bodyOut.clear();
    const std::wstring userAgentWide = HttpAuth_UserAgent();
    const wchar_t* path = HttpAuth_Path(endpoint);
    if (userAgentWide.empty() || path == nullptr || path[0] == L'\0') {
        DebugLog("ERROR", "http_auth_user_agent_fail", {});
        return HttpStatus::Fail;
    }

    // Query string is ASCII (url-encoded).
    std::wstring requestPath(path);
    if (!queryString.empty()) {
        requestPath += L'?';
        requestPath.append(queryString.begin(), queryString.end());
    }

    DebugLog(
        "INFO",
        std::string(logStage),
        std::format(
            "endpoint={} port={} secure={} query_len={}",
            HttpAuth_EndpointTag(endpoint),
            static_cast<int>(HttpAuth_Port()),
            HttpAuth_RequestFlags() != 0 ? 1 : 0,
            queryString.size()
        )
    );

    HINTERNET session = WinHttpOpen(
        userAgentWide.c_str(),
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (session == nullptr) {
        DebugLogWin32Error("http_winhttp_open_fail");
        return HttpStatus::Retry;
    }

    DWORD dwHttp2 = WINHTTP_PROTOCOL_FLAG_HTTP2;
    DWORD dwSslProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(session, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &dwHttp2, sizeof(dwHttp2));
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &dwSslProtocols, sizeof(dwSslProtocols));

    HINTERNET connect = WinHttpConnect(
        session,
        HttpAuth_Host(),
        static_cast<INTERNET_PORT>(HttpAuth_Port()),
        0
    );
    if (connect == nullptr) {
        DebugLogWin32Error("http_winhttp_connect_fail");
        WinHttpCloseHandle(session);
        return HttpStatus::Retry;
    }

    HINTERNET request = WinHttpOpenRequest(
        connect,
        L"GET",
        requestPath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        static_cast<DWORD>(HttpAuth_RequestFlags())
    );
    if (request == nullptr) {
        DebugLogWin32Error("http_winhttp_request_fail");
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return HttpStatus::Retry;
    }
    DWORD dwDecompression = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(request, WINHTTP_OPTION_DECOMPRESSION, &dwDecompression, sizeof(dwDecompression));

    const std::wstring headers = HttpAuth_BuildHeaders(apiKey, queryString);
    if (headers.empty()) {
        DebugLog("ERROR", "http_auth_headers_fail", {});
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return HttpStatus::Fail;
    }

    const BOOL sent = WinHttpSendRequest(
        request,
        headers.c_str(),
        static_cast<DWORD>(-1),
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0
    );
    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        DebugLogWin32Error("http_winhttp_send_fail");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return HttpStatus::Retry;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX
    );

    const HttpStatus mapped = statusCode >= 200 && statusCode < 300 ? HttpStatus::Ok
        : statusCode >= 400 && statusCode < 500 ? HttpStatus::Fail
        : HttpStatus::Retry;
    bodyOut = ReadHttpResponseBody(request, 262144);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    const char* mappedName = mapped == HttpStatus::Ok ? "Ok"
        : mapped == HttpStatus::Retry ? "Retry" : "Fail";
    DebugLog(
        "INFO",
        std::format("{}_done", logStage),
        std::format("status_code={} mapped_status={} body_size={}", statusCode, mappedName, bodyOut.size())
    );
    return mapped;
}

HttpStatus FetchWithRetry(const std::function<HttpStatus()>& attempt) {
    constexpr int tryMax = 4;
    for (int tryCount = 0; tryCount < tryMax; ++tryCount) {
        const HttpStatus status = attempt();
        if (status != HttpStatus::Retry) {
            return status;
        }
        const int sleepSec = tryCount == 0 ? 1 : (tryCount == 1 ? 2 : 4);
        std::this_thread::sleep_for(std::chrono::seconds(sleepSec));
    }
    return HttpStatus::Retry;
}

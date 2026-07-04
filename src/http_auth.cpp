#include "http_auth.h"

#include <chrono>
#include <format>
#include <string>
#include <string_view>

#include <windows.h>

#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

namespace {

constexpr const char* kIrClientVersion = "0.1.3";
constexpr const char* kIrUserAgentBase =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

constexpr wchar_t kScoreHost[] = L"ir.stellabms.xyz";
constexpr unsigned short kScorePort = 443;
constexpr unsigned long kScoreRequestFlags = 0x00800000; // WINHTTP_FLAG_SECURE

constexpr wchar_t kChartScorePath[] = L"/api/chart/score";
constexpr wchar_t kCourseScorePath[] = L"/api/course/score";
constexpr wchar_t kChartBoardPath[] = L"/api/chart/board";
constexpr wchar_t kCourseBoardPath[] = L"/api/course/board";
constexpr wchar_t kChartGhostPath[] = L"/api/chart/ghost";
constexpr wchar_t kIrLoginPath[] = L"/api/ir/login";

constexpr const char* kWebRankingUrlTemplate = "https://ir.stellabms.xyz/redirecthash/{hash}";

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.data() == nullptr || utf8.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8.data(),
        static_cast<int>(utf8.size()),
        nullptr,
        0
    );
    if (size <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8.data(),
        static_cast<int>(utf8.size()),
        wide.data(),
        size
    );
    return wide;
}

std::int64_t UnixTimeNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string GenerateHmacSha256(const std::string& key, const std::string& data) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    std::string result;
    DWORD cbHashObject = 0, cbData = 0, cbHash = 0;
    PBYTE pbHashObject = NULL;
    PBYTE pbHash = NULL;

    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
        return "";
    }

    if (!NT_SUCCESS(BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&cbHashObject, sizeof(DWORD), &cbData, 0))) {
        goto Cleanup;
    }
    if (!NT_SUCCESS(BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&cbHash, sizeof(DWORD), &cbData, 0))) {
        goto Cleanup;
    }

    pbHashObject = new BYTE[cbHashObject];
    pbHash = new BYTE[cbHash];

    if (!NT_SUCCESS(BCryptCreateHash(hAlg, &hHash, pbHashObject, cbHashObject, (PUCHAR)key.data(), static_cast<ULONG>(key.size()), 0))) {
        goto Cleanup;
    }
    if (!NT_SUCCESS(BCryptHashData(hHash, (PUCHAR)data.data(), static_cast<ULONG>(data.size()), 0))) {
        goto Cleanup;
    }
    if (!NT_SUCCESS(BCryptFinishHash(hHash, pbHash, cbHash, 0))) {
        goto Cleanup;
    }

    for (DWORD i = 0; i < cbHash; i++) {
        result += std::format("{:02x}", pbHash[i]);
    }

Cleanup:
    if (hAlg) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    if (hHash) {
        BCryptDestroyHash(hHash);
    }
    delete[] pbHashObject;
    delete[] pbHash;
    return result;
}

} // namespace

std::wstring HttpAuth_UserAgent() {
    return Utf8ToWide(std::format("{} STELLAVERSE_IR_{}", kIrUserAgentBase, kIrClientVersion));
}

std::wstring HttpAuth_BuildHeaders(std::string_view apiKey, std::string_view payload) {
    if (apiKey.empty()) {
        return {};
    }
    const std::string timestamp = std::to_string(UnixTimeNow());
    const std::string payloadToSign = timestamp + "." + std::string(payload);
    const std::string signature = GenerateHmacSha256(std::string(apiKey), payloadToSign);
    if (signature.empty()) {
        return {};
    }
    return std::format(
        L"Content-Type: application/json\r\nUser-Agent: {}\r\nx-api-key: {}\r\nx-timestamp: {}\r\nx-signature: {}\r\n",
        HttpAuth_UserAgent(),
        Utf8ToWide(apiKey),
        Utf8ToWide(timestamp),
        Utf8ToWide(signature)
    );
}

const wchar_t* HttpAuth_Host() {
    return kScoreHost;
}

unsigned short HttpAuth_Port() {
    return kScorePort;
}

unsigned long HttpAuth_RequestFlags() {
    return kScoreRequestFlags;
}

const wchar_t* HttpAuth_Path(HttpAuthEndpoint endpoint) {
    switch (endpoint) {
    case HttpAuthEndpoint::ChartScore: return kChartScorePath;
    case HttpAuthEndpoint::CourseScore: return kCourseScorePath;
    case HttpAuthEndpoint::ChartBoard: return kChartBoardPath;
    case HttpAuthEndpoint::CourseBoard: return kCourseBoardPath;
    case HttpAuthEndpoint::ChartGhost: return kChartGhostPath;
    case HttpAuthEndpoint::IrLogin: return kIrLoginPath;
    }
    return L"";
}

const char* HttpAuth_WebRankingUrlTemplate() {
    return kWebRankingUrlTemplate;
}

const char* HttpAuth_EndpointTag(HttpAuthEndpoint endpoint) {
    switch (endpoint) {
    case HttpAuthEndpoint::ChartScore:
    case HttpAuthEndpoint::ChartBoard:
    case HttpAuthEndpoint::ChartGhost:
        return "chart";
    case HttpAuthEndpoint::CourseScore:
    case HttpAuthEndpoint::CourseBoard:
        return "course";
    case HttpAuthEndpoint::IrLogin:
        return "ir_login";
    }
    return "unknown";
}

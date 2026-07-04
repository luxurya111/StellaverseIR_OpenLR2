#include "log.h"

#include "constants.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>

#include <windows.h>

namespace {

std::mutex g_debugLogMutex;
bool g_debugLogReset = false;

std::filesystem::path DebugLogPath() {
    return GetModuleDirectory() / "debug.log";
}

std::string DebugTimestampLocal() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm localTm{};
    localtime_s(&localTm, &t);
    return std::format(
        "{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
        localTm.tm_year + 1900,
        localTm.tm_mon + 1,
        localTm.tm_mday,
        localTm.tm_hour,
        localTm.tm_min,
        localTm.tm_sec
    );
}

void MaybeRotateDebugLog() {
    const std::filesystem::path logPath = DebugLogPath();
    if (logPath.empty()) {
        return;
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(logPath, ec);
    if (ec || size <= kDebugLogRotateBytes) {
        return;
    }
    const std::filesystem::path oldPath = logPath.parent_path() / "debug.log.old";
    std::filesystem::remove(oldPath, ec);
    std::filesystem::rename(logPath, oldPath, ec);
}

void TruncateDebugLogUnlocked() {
    const std::filesystem::path logPath = DebugLogPath();
    if (logPath.empty()) {
        return;
    }
    HANDLE file = CreateFileW(
        logPath.wstring().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
}

void DebugLogAppendUnlocked(std::string_view level, std::string_view stage, std::string_view fields) {
    const std::filesystem::path logPath = DebugLogPath();
    if (logPath.empty()) {
        return;
    }
    MaybeRotateDebugLog();
    const std::string line = fields.empty()
        ? std::format("{} | {} | {}\n", DebugTimestampLocal(), level, stage)
        : std::format("{} | {} | {} | {}\n", DebugTimestampLocal(), level, stage, fields);

    HANDLE file = CreateFileW(
        logPath.wstring().c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(file);
}

} // namespace

std::filesystem::path GetModuleDirectory() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetModuleDirectory),
            &module)) {
        return {};
    }
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(module, path, MAX_PATH) == 0) {
        return {};
    }
    return std::filesystem::path(path).parent_path();
}

void DebugLog(std::string_view level, std::string_view stage, std::string_view fields) {
    std::lock_guard lock(g_debugLogMutex);
    DebugLogAppendUnlocked(level, stage, fields);
}

void DebugLogWin32Error(std::string_view stage, unsigned long err) {
    if (err == 0) {
        err = GetLastError();
    }
    DebugLog("ERROR", stage, std::format("win32={}", err));
}

void ResetDebugLogOnce() {
    std::lock_guard lock(g_debugLogMutex);
    if (g_debugLogReset) {
        return;
    }
    g_debugLogReset = true;
    TruncateDebugLogUnlocked();
    DebugLogAppendUnlocked("INFO", "session_start", {});
}

std::string LoadApiKey() {
    const std::filesystem::path keyPath = GetModuleDirectory() / "key.txt";
    std::ifstream file(keyPath);
    if (!file) {
        return {};
    }
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (contents.size() > kApiKeyMaxLen) {
        contents.resize(kApiKeyMaxLen);
    }
    return contents;
}

std::string HashPrefix8(std::string_view hash) {
    if (hash.empty()) {
        return {};
    }
    return std::string(hash.substr(0, std::min<std::size_t>(hash.size(), 8)));
}

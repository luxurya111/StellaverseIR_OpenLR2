#pragma once

#include <filesystem>
#include <string>
#include <string_view>

std::filesystem::path GetModuleDirectory();
std::string LoadApiKey();
std::string HashPrefix8(std::string_view hash);

void DebugLog(std::string_view level, std::string_view stage, std::string_view fields = {});
void DebugLogWin32Error(std::string_view stage, unsigned long err = 0);
// Truncate debug.log once per process. Call only from LoginV1 (OpenLR2 startup).
void ResetDebugLogOnce();

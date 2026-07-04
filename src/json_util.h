#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

template<class T>
std::optional<T> JsonField(const json& j, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        const auto it = j.find(key);
        if (it == j.end() || it->is_null()) {
            continue;
        }
        try {
            return it->template get<T>();
        } catch (const json::exception&) {
        }
    }
    return std::nullopt;
}

template<class T>
T JsonFieldOr(const json& j, std::initializer_list<const char*> keys, T fallback) {
    return JsonField<T>(j, keys).value_or(std::move(fallback));
}

inline bool ReadJsonFile(const std::filesystem::path& path, json& out) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    try {
        in >> out;
    } catch (const json::exception&) {
        return false;
    }
    return true;
}

inline bool WriteJsonFile(const std::filesystem::path& path, const json& body) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    out << body.dump();
    return static_cast<bool>(out);
}

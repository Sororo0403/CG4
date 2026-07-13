#pragma once

#include "core/AssetManager.h"

#include <algorithm>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <string>

namespace PathUtils {

inline std::filesystem::path ResolveAssetPath(const std::wstring& path) {
    try {
        return AssetManager::ResolvePathStrict(std::filesystem::path(path));
    } catch (const std::exception&) {
        return {};
    }
}

inline std::wstring NormalizePathKey(const std::filesystem::path& path) {
    std::wstring key;
    try {
        key = path.lexically_normal().wstring();
    } catch (const std::exception&) {
        return {};
    }
#ifdef _WIN32
    try {
        std::transform(key.begin(), key.end(), key.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    } catch (const std::exception&) {
        return {};
    }
#endif
    return key;
}

inline std::wstring NormalizeKey(const std::wstring& source) {
    std::wstring key;
    try {
        key = source;
    } catch (const std::exception&) {
        return {};
    }
#ifdef _WIN32
    try {
        std::transform(key.begin(), key.end(), key.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    } catch (const std::exception&) {
        return {};
    }
#endif
    return key;
}

} // namespace PathUtils

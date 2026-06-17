#pragma once

#include "core/AssetManager.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>

namespace PathUtils {

inline std::filesystem::path ResolveAssetPath(const std::wstring &path) {
    return AssetManager::ResolvePathStrict(std::filesystem::path(path));
}

inline std::wstring NormalizePathKey(const std::filesystem::path &path) {
    std::wstring key = path.lexically_normal().wstring();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
#endif
    return key;
}

inline std::wstring NormalizeKey(const std::wstring &source) {
    std::wstring key = source;
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
#endif
    return key;
}

} // namespace PathUtils

#include "core/AssetHotReloader.h"

#include <algorithm>
#include <cwctype>

namespace {

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return value;
}

} // namespace

bool AssetHotReloader::WatchFile(const std::filesystem::path &path,
                                 ReloadCallback callback) {
    if (!callback) {
        return false;
    }

    std::filesystem::file_time_type lastWriteTime{};
    const std::filesystem::path normalized = NormalizePath(path);
    if (!TryGetLastWriteTime(normalized, lastWriteTime)) {
        return false;
    }

    WatchedFile file{};
    file.path = normalized;
    file.lastWriteTime = lastWriteTime;
    file.callback = std::move(callback);
    watchedFiles_[Lowercase(normalized.wstring())] = std::move(file);
    return true;
}

bool AssetHotReloader::WatchDirectory(
    const std::filesystem::path &directory,
    const std::vector<std::wstring> &extensions, ReloadCallback callback) {
    if (!callback || !std::filesystem::exists(directory)) {
        return false;
    }

    bool watchedAny = false;
    for (const std::filesystem::directory_entry &entry :
         std::filesystem::recursive_directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (!extensions.empty() && !HasExtension(entry.path(), extensions)) {
            continue;
        }
        watchedAny = WatchFile(entry.path(), callback) || watchedAny;
    }
    return watchedAny;
}

void AssetHotReloader::Unwatch(const std::filesystem::path &path) {
    watchedFiles_.erase(Lowercase(NormalizePath(path).wstring()));
}

void AssetHotReloader::Clear() { watchedFiles_.clear(); }

void AssetHotReloader::Poll() {
    for (auto &[_, file] : watchedFiles_) {
        std::filesystem::file_time_type lastWriteTime{};
        if (!TryGetLastWriteTime(file.path, lastWriteTime)) {
            continue;
        }
        if (lastWriteTime == file.lastWriteTime) {
            continue;
        }
        file.lastWriteTime = lastWriteTime;
        file.callback(file.path);
    }
}

std::filesystem::path
AssetHotReloader::NormalizePath(const std::filesystem::path &path) {
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) {
        return path.lexically_normal();
    }
    return absolute.lexically_normal();
}

bool AssetHotReloader::TryGetLastWriteTime(
    const std::filesystem::path &path,
    std::filesystem::file_time_type &lastWriteTime) {
    std::error_code error;
    lastWriteTime = std::filesystem::last_write_time(path, error);
    return !error;
}

bool AssetHotReloader::HasExtension(
    const std::filesystem::path &path,
    const std::vector<std::wstring> &extensions) {
    const std::wstring extension = Lowercase(path.extension().wstring());
    for (std::wstring candidate : extensions) {
        if (!candidate.empty() && candidate.front() != L'.') {
            candidate.insert(candidate.begin(), L'.');
        }
        if (extension == Lowercase(candidate)) {
            return true;
        }
    }
    return false;
}

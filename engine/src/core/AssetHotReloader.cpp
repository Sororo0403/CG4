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
    file.key = Lowercase(normalized.wstring());
    file.path = normalized;
    file.lastWriteTime = lastWriteTime;
    file.callback = std::move(callback);
    const auto existing =
        std::find_if(watchedFiles_.begin(), watchedFiles_.end(),
                     [&](const WatchedFile &watched) {
                         return watched.key == file.key;
                     });
    if (existing != watchedFiles_.end()) {
        *existing = std::move(file);
    } else {
        watchedFiles_.push_back(std::move(file));
    }
    return true;
}

bool AssetHotReloader::WatchDirectory(
    const std::filesystem::path &directory,
    const std::vector<std::wstring> &extensions, ReloadCallback callback) {
    if (!callback) {
        return false;
    }

    std::error_code error;
    if (!std::filesystem::exists(directory, error) || error) {
        return false;
    }

    bool watchedAny = false;
    std::filesystem::recursive_directory_iterator it(
        directory, std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end{};
    if (error) {
        return false;
    }

    while (it != end) {
        const std::filesystem::directory_entry entry = *it;
        error.clear();
        if (!entry.is_regular_file(error) || error) {
            it.increment(error);
            if (error) {
                break;
            }
            continue;
        }
        if (!extensions.empty() && !HasExtension(entry.path(), extensions)) {
            it.increment(error);
            if (error) {
                break;
            }
            continue;
        }
        watchedAny = WatchFile(entry.path(), callback) || watchedAny;
        it.increment(error);
        if (error) {
            break;
        }
    }
    return watchedAny;
}

void AssetHotReloader::Unwatch(const std::filesystem::path &path) {
    const std::wstring key = Lowercase(NormalizePath(path).wstring());
    watchedFiles_.erase(
        std::remove_if(watchedFiles_.begin(), watchedFiles_.end(),
                       [&](const WatchedFile &file) {
                           return file.key == key;
                       }),
        watchedFiles_.end());
}

void AssetHotReloader::Clear() { watchedFiles_.clear(); }

void AssetHotReloader::Poll() {
    for (WatchedFile &file : watchedFiles_) {
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

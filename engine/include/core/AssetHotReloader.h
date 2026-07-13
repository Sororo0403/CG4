#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class AssetHotReloader {
public:
    using ReloadCallback = std::function<void(const std::filesystem::path&)>;

    bool WatchFile(const std::filesystem::path& path, ReloadCallback callback);
    bool WatchDirectory(const std::filesystem::path& directory,
                        const std::vector<std::wstring>& extensions, ReloadCallback callback);
    void Unwatch(const std::filesystem::path& path);
    void Clear();

    void Poll();
    size_t GetWatchedFileCount() const {
        return watchedFiles_.size();
    }

private:
    struct WatchedFile {
        std::wstring key;
        std::filesystem::path path;
        std::filesystem::file_time_type lastWriteTime{};
        ReloadCallback callback;
    };

    static std::filesystem::path NormalizePath(const std::filesystem::path& path);
    static bool TryGetLastWriteTime(const std::filesystem::path& path,
                                    std::filesystem::file_time_type& lastWriteTime);
    static bool HasExtension(const std::filesystem::path& path,
                             const std::vector<std::wstring>& extensions);

    std::vector<WatchedFile> watchedFiles_;
};

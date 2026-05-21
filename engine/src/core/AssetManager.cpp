#include "core/AssetManager.h"
#include <system_error>

namespace {

std::filesystem::path gAssetRoot = std::filesystem::current_path();

std::filesystem::path ResolveRoot(const std::filesystem::path &path) {
    if (path.is_absolute()) {
        return AssetManager::ResolvePath(path);
    }
    return AssetManager::ResolvePath(std::filesystem::current_path() / path);
}

} // namespace

void AssetManager::SetAssetRoot(std::filesystem::path assetRoot) {
    gAssetRoot = ResolveRoot(assetRoot);
}

const std::filesystem::path &AssetManager::GetAssetRoot() {
    return gAssetRoot;
}

std::filesystem::path
AssetManager::ResolvePath(const std::filesystem::path &relativePath) {
    const std::filesystem::path normalized = relativePath.lexically_normal();
    if (normalized.is_absolute()) {
        return Canonicalize(normalized);
    }

    const std::filesystem::path rooted = gAssetRoot / normalized;
    if (std::filesystem::exists(rooted)) {
        return Canonicalize(rooted);
    }

    for (std::filesystem::path dir = gAssetRoot; !dir.empty();
         dir = dir.parent_path()) {
        const std::filesystem::path candidate = dir / normalized;
        if (std::filesystem::exists(candidate)) {
            return Canonicalize(candidate);
        }

        if (dir == dir.root_path()) {
            break;
        }
    }

    return Canonicalize(rooted);
}

std::filesystem::path
AssetManager::Canonicalize(const std::filesystem::path &path) {
    std::error_code ec;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return canonical;
    }
    return path.lexically_normal();
}

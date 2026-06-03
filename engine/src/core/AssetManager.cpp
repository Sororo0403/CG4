#include "core/AssetManager.h"
#include <mutex>
#include <system_error>

namespace {

std::filesystem::path SafeCurrentPath() {
    std::error_code ec;
    const std::filesystem::path path = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path(L".") : path;
}

std::filesystem::path gAssetRoot;
std::mutex gAssetRootMutex;

std::filesystem::path CanonicalizePath(const std::filesystem::path &path) {
    std::error_code ec;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return canonical;
    }
    return path.lexically_normal();
}

std::filesystem::path ResolveRoot(const std::filesystem::path &path) {
    return CanonicalizePath(path.is_absolute() ? path : SafeCurrentPath() / path);
}

bool HasParentTraversal(const std::filesystem::path &path) {
    const std::filesystem::path parent(L"..");
    for (const std::filesystem::path &part : path) {
        if (part == parent) {
            return true;
        }
    }
    return false;
}

bool IsWithinRoot(const std::filesystem::path &root,
                  const std::filesystem::path &path) {
    std::error_code ec;
    const std::filesystem::path relative =
        std::filesystem::relative(path, root, ec);
    return !ec && !relative.is_absolute() && !HasParentTraversal(relative);
}

bool ExistsNoThrow(const std::filesystem::path &path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool LooksLikeRepositoryAssetRoot(const std::filesystem::path &path) {
    return ExistsNoThrow(path / L"engine" / L"resources") &&
           (ExistsNoThrow(path / L"1000.slnx") ||
            ExistsNoThrow(path / L"build.cmd") ||
            ExistsNoThrow(path / L"1000" / L"resources"));
}

bool HasLocalResources(const std::filesystem::path &path) {
    return ExistsNoThrow(path / L"resources");
}

template <typename Predicate>
std::filesystem::path FindAncestor(const std::filesystem::path &start,
                                   Predicate predicate) {
    for (std::filesystem::path dir = start; !dir.empty();
         dir = dir.parent_path()) {
        if (predicate(dir)) {
            return CanonicalizePath(dir);
        }

        if (dir == dir.root_path()) {
            break;
        }
    }
    return {};
}

std::filesystem::path FindDefaultAssetRoot() {
    const std::filesystem::path start = ResolveRoot(SafeCurrentPath());
    if (const std::filesystem::path repoRoot =
            FindAncestor(start, LooksLikeRepositoryAssetRoot);
        !repoRoot.empty()) {
        return repoRoot;
    }
    if (const std::filesystem::path localResourceRoot =
            FindAncestor(start, HasLocalResources);
        !localResourceRoot.empty()) {
        return localResourceRoot;
    }
    return start;
}

} // namespace

void AssetManager::SetAssetRoot(std::filesystem::path assetRoot) {
    const std::filesystem::path resolvedRoot = ResolveRoot(assetRoot);
    std::lock_guard<std::mutex> lock(gAssetRootMutex);
    gAssetRoot = resolvedRoot;
}

std::filesystem::path AssetManager::GetAssetRoot() {
    std::lock_guard<std::mutex> lock(gAssetRootMutex);
    if (gAssetRoot.empty()) {
        gAssetRoot = FindDefaultAssetRoot();
    }
    return gAssetRoot;
}

std::filesystem::path
AssetManager::ResolvePath(const std::filesystem::path &relativePath) {
    const std::filesystem::path normalized = relativePath.lexically_normal();
    if (normalized.is_absolute()) {
        return Canonicalize(normalized);
    }

    const std::filesystem::path assetRoot = GetAssetRoot();

    const std::filesystem::path rooted = assetRoot / normalized;
    std::error_code ec;
    if (std::filesystem::exists(rooted, ec)) {
        return Canonicalize(rooted);
    }

    for (std::filesystem::path dir = assetRoot; !dir.empty();
         dir = dir.parent_path()) {
        const std::filesystem::path candidate = dir / normalized;
        ec.clear();
        if (std::filesystem::exists(candidate, ec)) {
            return Canonicalize(candidate);
        }

        if (dir == dir.root_path()) {
            break;
        }
    }

    return Canonicalize(rooted);
}

std::filesystem::path
AssetManager::ResolvePathStrict(const std::filesystem::path &relativePath) {
    if (relativePath.empty()) {
        return {};
    }

    const std::filesystem::path assetRoot = Canonicalize(GetAssetRoot());
    const std::filesystem::path normalized = relativePath.lexically_normal();

    if (normalized.is_absolute()) {
        const std::filesystem::path canonical = Canonicalize(normalized);
        return IsWithinRoot(assetRoot, canonical) ? canonical
                                                 : std::filesystem::path{};
    }

    if (normalized.has_root_name() || normalized.has_root_directory() ||
        HasParentTraversal(normalized)) {
        return {};
    }

    const std::filesystem::path rooted = Canonicalize(assetRoot / normalized);
    return IsWithinRoot(assetRoot, rooted) ? rooted : std::filesystem::path{};
}

std::filesystem::path
AssetManager::Canonicalize(const std::filesystem::path &path) {
    return CanonicalizePath(path);
}

#include "core/AssetManager.h"

#include <algorithm>
#include <exception>
#include <mutex>
#include <system_error>

namespace {

std::filesystem::path SafeCurrentPath() {
    std::error_code ec;
    try {
        const std::filesystem::path path = std::filesystem::current_path(ec);
        return ec ? std::filesystem::path(L".") : path;
    } catch (const std::exception&) {
        return std::filesystem::path(L".");
    }
}

std::filesystem::path gAssetRoot;
std::mutex gAssetRootMutex;

std::filesystem::path CanonicalizePath(const std::filesystem::path& path) {
    std::error_code ec;
    try {
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
        if (!ec) {
            return canonical;
        }
        return path.lexically_normal();
    } catch (const std::exception&) {
        return path;
    }
}

std::filesystem::path ResolveRoot(const std::filesystem::path& path) {
    try {
        return CanonicalizePath(path.is_absolute() ? path : SafeCurrentPath() / path);
    } catch (const std::exception&) {
        return CanonicalizePath(SafeCurrentPath());
    }
}

bool HasParentTraversal(const std::filesystem::path& path) {
    const std::filesystem::path parent(L"..");
    try {
        return std::any_of(path.begin(), path.end(),
                           [&parent](const std::filesystem::path& part) { return part == parent; });
    } catch (const std::exception&) {
        return true;
    }
}

bool IsWithinRoot(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::error_code ec;
    try {
        const std::filesystem::path relative = std::filesystem::relative(path, root, ec);
        return !ec && !relative.is_absolute() && !HasParentTraversal(relative);
    } catch (const std::exception&) {
        return false;
    }
}

bool ExistsNoThrow(const std::filesystem::path& path) {
    std::error_code ec;
    try {
        return std::filesystem::exists(path, ec);
    } catch (const std::exception&) {
        return false;
    }
}

bool LooksLikeRepositoryAssetRoot(const std::filesystem::path& path) {
    try {
        return ExistsNoThrow(path / L"engine" / L"resources") &&
               (ExistsNoThrow(path / L"build.bat") || ExistsNoThrow(path / L"build.ps1") ||
                ExistsNoThrow(path / L"build.cmd"));
    } catch (const std::exception&) {
        return false;
    }
}

bool HasLocalResources(const std::filesystem::path& path) {
    try {
        return ExistsNoThrow(path / L"resources");
    } catch (const std::exception&) {
        return false;
    }
}

template <typename Predicate>
std::filesystem::path FindAncestor(const std::filesystem::path& start, Predicate predicate) {
    try {
        for (std::filesystem::path dir = start; !dir.empty(); dir = dir.parent_path()) {
            if (predicate(dir)) {
                return CanonicalizePath(dir);
            }

            if (dir == dir.root_path()) {
                break;
            }
        }
    } catch (const std::exception&) {
        return {};
    }
    return {};
}

std::filesystem::path FindDefaultAssetRoot() {
    const std::filesystem::path start = ResolveRoot(SafeCurrentPath());
    if (const std::filesystem::path repoRoot = FindAncestor(start, LooksLikeRepositoryAssetRoot);
        !repoRoot.empty()) {
        return repoRoot;
    }
    if (const std::filesystem::path localResourceRoot = FindAncestor(start, HasLocalResources);
        !localResourceRoot.empty()) {
        return localResourceRoot;
    }
    return start;
}

} // namespace

void AssetManager::SetAssetRoot(std::filesystem::path assetRoot) {
    const std::filesystem::path resolvedRoot = ResolveRoot(assetRoot);
    std::lock_guard<std::mutex> lock(gAssetRootMutex);
    try {
        gAssetRoot = resolvedRoot;
    } catch (const std::exception&) {
        gAssetRoot.clear();
    }
}

std::filesystem::path AssetManager::GetAssetRoot() {
    std::lock_guard<std::mutex> lock(gAssetRootMutex);
    if (gAssetRoot.empty()) {
        try {
            gAssetRoot = FindDefaultAssetRoot();
        } catch (const std::exception&) {
            gAssetRoot = std::filesystem::path(L".");
        }
    }
    return gAssetRoot;
}

std::filesystem::path AssetManager::ResolvePath(const std::filesystem::path& relativePath) {
    std::filesystem::path normalized;
    try {
        normalized = relativePath.lexically_normal();
    } catch (const std::exception&) {
        return GetAssetRoot();
    }
    if (normalized.is_absolute()) {
        return Canonicalize(normalized);
    }

    const std::filesystem::path assetRoot = GetAssetRoot();

    std::filesystem::path rooted;
    try {
        rooted = assetRoot / normalized;
    } catch (const std::exception&) {
        return assetRoot;
    }
    if (ExistsNoThrow(rooted)) {
        return Canonicalize(rooted);
    }

    try {
        for (std::filesystem::path dir = assetRoot; !dir.empty(); dir = dir.parent_path()) {
            const std::filesystem::path candidate = dir / normalized;
            if (ExistsNoThrow(candidate)) {
                return Canonicalize(candidate);
            }

            if (dir == dir.root_path()) {
                break;
            }
        }
    } catch (const std::exception&) {
        return Canonicalize(rooted);
    }

    return Canonicalize(rooted);
}

std::filesystem::path AssetManager::ResolvePathStrict(const std::filesystem::path& relativePath) {
    if (relativePath.empty()) {
        return {};
    }

    const std::filesystem::path assetRoot = Canonicalize(GetAssetRoot());
    std::filesystem::path normalized;
    try {
        normalized = relativePath.lexically_normal();
    } catch (const std::exception&) {
        return {};
    }

    if (normalized.is_absolute()) {
        const std::filesystem::path canonical = Canonicalize(normalized);
        return IsWithinRoot(assetRoot, canonical) ? canonical : std::filesystem::path{};
    }

    if (normalized.has_root_name() || normalized.has_root_directory() ||
        HasParentTraversal(normalized)) {
        return {};
    }

    std::filesystem::path resolvedPath;
    try {
        resolvedPath = Canonicalize(assetRoot / normalized);
    } catch (const std::exception&) {
        return {};
    }
    return IsWithinRoot(assetRoot, resolvedPath) ? resolvedPath : std::filesystem::path{};
}

std::filesystem::path AssetManager::Canonicalize(const std::filesystem::path& path) {
    return CanonicalizePath(path);
}

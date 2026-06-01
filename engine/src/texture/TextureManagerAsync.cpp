#include "texture/TextureManager.h"
#include "core/AssetManager.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/SrvManager.h"
#include "texture/Texture.h"
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <future>
#include <limits>
#include <vector>

static std::filesystem::path ResolveTexturePath(const std::wstring &path) {
    return AssetManager::ResolvePath(std::filesystem::path(path));
}

static std::wstring NormalizePathKey(const std::filesystem::path &path) {
    std::wstring key = path.lexically_normal().wstring();

#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
#endif

    return key;
}

static TextureManager::DecodedTexture DecodeTextureFileForAsync(
    const std::wstring &filePath) {
    const std::filesystem::path resolvedPath = ResolveTexturePath(filePath);
    TextureManager::DecodedTexture decoded{};
    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec)) {
        return decoded;
    }

    decoded.pathKey = NormalizePathKey(resolvedPath);
    const std::wstring ext = resolvedPath.extension().wstring();

    if (_wcsicmp(ext.c_str(), L".dds") == 0) {
        if (FAILED(DirectX::LoadFromDDSFile(resolvedPath.c_str(),
                                            DirectX::DDS_FLAGS_NONE,
                                            &decoded.metadata,
                                            decoded.scratch))) {
            return {};
        }
    } else {
        if (FAILED(DirectX::LoadFromWICFile(resolvedPath.c_str(),
                                            DirectX::WIC_FLAGS_IGNORE_SRGB,
                                            &decoded.metadata,
                                            decoded.scratch))) {
            return {};
        }
    }

    decoded.succeeded =
        decoded.scratch.GetImages() != nullptr &&
        decoded.scratch.GetImageCount() > 0 && !decoded.pathKey.empty();
    return decoded;
}

using namespace DirectX;
using Microsoft::WRL::ComPtr;

static constexpr size_t kMaxCompletedAsyncRequestHistory = 256;

uint32_t TextureManager::RequestAsyncLoad(const std::wstring &filePath) {
    PruneCompletedAsyncRequests();

    const std::filesystem::path resolvedPath = ResolveTexturePath(filePath);
    const std::wstring pathKey = NormalizePathKey(resolvedPath);
    auto cached = filePathToTextureId_.find(pathKey);
    if (cached != filePathToTextureId_.end()) {
        if (!IsValidTextureId(cached->second) ||
            cached->second == whiteTextureId_) {
            filePathToTextureId_.erase(cached);
        } else {
            AsyncTextureRequest request{};
            request.requestId = AllocateAsyncRequestId();
            request.textureId = cached->second;
            request.completed = true;
            asyncRequests_.push_back(std::move(request));
            const uint32_t requestId = asyncRequests_.back().requestId;
            PruneCompletedAsyncRequests();
            return requestId;
        }
    }

    AsyncTextureRequest request{};
    request.requestId = AllocateAsyncRequestId();
    request.future = std::async(std::launch::async, [filePath]() {
        return DecodeTextureFileForAsync(filePath);
    });
    asyncRequests_.push_back(std::move(request));
    return asyncRequests_.back().requestId;
}

std::vector<uint32_t> TextureManager::RequestAsyncLoadBatch(
    const std::vector<std::wstring> &filePaths) {
    std::vector<uint32_t> requestIds;
    requestIds.reserve(filePaths.size());
    for (const std::wstring &filePath : filePaths) {
        requestIds.push_back(RequestAsyncLoad(filePath));
    }
    return requestIds;
}

void TextureManager::UpdateAsyncLoads() {
    if (!dxCommon_ || !dxCommon_->IsCommandListRecording()) {
        return;
    }

    for (AsyncTextureRequest &request : asyncRequests_) {
        if (request.completed || request.failed || !request.future.valid()) {
            continue;
        }

        if (request.future.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
            continue;
        }

        DecodedTexture decoded = request.future.get();
        if (!decoded.succeeded) {
            request.failed = true;
            continue;
        }

        auto cached = filePathToTextureId_.find(decoded.pathKey);
        if (cached != filePathToTextureId_.end() &&
            IsValidTextureId(cached->second) &&
            cached->second != whiteTextureId_) {
            request.textureId = cached->second;
        } else {
            if (cached != filePathToTextureId_.end()) {
                filePathToTextureId_.erase(cached);
            }
            request.textureId =
                CreateTexture(decoded.scratch.GetImages(),
                              decoded.scratch.GetImageCount(),
                              decoded.metadata);
            if (!IsValidTextureId(request.textureId) ||
                request.textureId == whiteTextureId_) {
                request.failed = true;
                continue;
            }
            filePathToTextureId_[decoded.pathKey] = request.textureId;
        }
        request.completed = true;
    }

    PruneCompletedAsyncRequests();
}

bool TextureManager::IsAsyncLoadComplete(uint32_t requestId) const {
    for (const AsyncTextureRequest &request : asyncRequests_) {
        if (request.requestId == requestId) {
            return request.completed;
        }
    }
    return false;
}

std::optional<uint32_t>
TextureManager::GetAsyncTextureId(uint32_t requestId) const {
    for (const AsyncTextureRequest &request : asyncRequests_) {
        if (request.requestId == requestId && request.completed) {
            return request.textureId;
        }
    }
    return std::nullopt;
}

bool TextureManager::HasAsyncLoadFailed(uint32_t requestId) const {
    for (const AsyncTextureRequest &request : asyncRequests_) {
        if (request.requestId == requestId) {
            return request.failed;
        }
    }
    return false;
}

void TextureManager::PruneCompletedAsyncRequests() {
    size_t completedCount = 0;
    for (const AsyncTextureRequest &request : asyncRequests_) {
        if (request.completed || request.failed) {
            ++completedCount;
        }
    }
    if (completedCount <= kMaxCompletedAsyncRequestHistory) {
        return;
    }

    size_t removeCount = completedCount - kMaxCompletedAsyncRequestHistory;
    asyncRequests_.erase(
        std::remove_if(
            asyncRequests_.begin(), asyncRequests_.end(),
            [&removeCount](const AsyncTextureRequest &request) {
                if (removeCount == 0 ||
                    (!request.completed && !request.failed)) {
                    return false;
                }
                --removeCount;
                return true;
            }),
        asyncRequests_.end());
}

uint32_t TextureManager::AllocateAsyncRequestId() {
    if (asyncRequests_.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)()) - 1u) {
        return 0;
    }

    for (;;) {
        if (nextAsyncRequestId_ == 0) {
            nextAsyncRequestId_ = 1;
        }
        const uint32_t candidate = nextAsyncRequestId_++;
        const auto it = std::find_if(
            asyncRequests_.begin(), asyncRequests_.end(),
            [candidate](const AsyncTextureRequest &request) {
                return request.requestId == candidate;
            });
        if (it == asyncRequests_.end()) {
            return candidate;
        }
    }
}

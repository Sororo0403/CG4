#include "texture/TextureManager.h"
#include "core/ComInitialization.h"
#include "core/PathUtils.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/SrvManager.h"
#include "texture/Texture.h"
#include "texture/TextureLimits.h"
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <limits>
#include <thread>
#include <vector>

static TextureManager::DecodedTexture DecodeResolvedTextureFileForAsync(
    const std::filesystem::path &resolvedPath) {
    TextureManager::DecodedTexture decoded{};
    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec) ||
        !TextureLimits::IsFileWithinInputBudget(resolvedPath)) {
        return decoded;
    }

    decoded.pathKey = PathUtils::NormalizePathKey(resolvedPath);
    const std::wstring ext = resolvedPath.extension().wstring();
    const bool isDds = _wcsicmp(ext.c_str(), L".dds") == 0;

    if (isDds) {
        if (FAILED(DirectX::GetMetadataFromDDSFile(
                resolvedPath.c_str(), DirectX::DDS_FLAGS_NONE,
                decoded.metadata)) ||
            !TextureLimits::IsMetadataWithinBudget(decoded.metadata)) {
            return {};
        }
        if (FAILED(DirectX::LoadFromDDSFile(resolvedPath.c_str(),
                                            DirectX::DDS_FLAGS_NONE,
                                            &decoded.metadata,
                                            decoded.scratch))) {
            return {};
        }
    } else {
        ScopedComInitialization com;
        if (!com.IsUsable()) {
            return {};
        }
        if (FAILED(DirectX::GetMetadataFromWICFile(
                resolvedPath.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB,
                decoded.metadata)) ||
            !TextureLimits::IsMetadataWithinBudget(decoded.metadata)) {
            return {};
        }
        if (FAILED(DirectX::LoadFromWICFile(resolvedPath.c_str(),
                                            DirectX::WIC_FLAGS_IGNORE_SRGB,
                                            &decoded.metadata,
                                            decoded.scratch))) {
            return {};
        }
    }

    decoded.succeeded =
        decoded.scratch.GetImages() != nullptr &&
        decoded.scratch.GetImageCount() > 0 && !decoded.pathKey.empty() &&
        TextureLimits::IsMetadataWithinBudget(
            decoded.metadata, decoded.scratch.GetImageCount()) &&
        TextureLimits::AreImagesWithinDecodedBudget(
            decoded.scratch.GetImages(), decoded.scratch.GetImageCount());
    return decoded;
}

using namespace DirectX;
using Microsoft::WRL::ComPtr;

static constexpr size_t kMaxCompletedAsyncRequestHistory = 256;
static constexpr size_t kMaxInFlightAsyncLoads = 4;

uint32_t TextureManager::RequestAsyncLoad(const std::wstring &filePath) {
    if (dxCommon_ == nullptr || dxCommon_->GetDevice() == nullptr ||
        srvManager_ == nullptr || !IsValidTextureId(whiteTextureId_)) {
        return 0;
    }

    PruneCompletedAsyncRequests();

    const std::filesystem::path resolvedPath =
        PathUtils::ResolveAssetPath(filePath);
    const std::wstring pathKey = PathUtils::NormalizePathKey(resolvedPath);
    auto cached = filePathToTextureId_.find(pathKey);
    if (cached != filePathToTextureId_.end()) {
        if (!IsValidTextureId(cached->second) ||
            cached->second == whiteTextureId_) {
            filePathToTextureId_.erase(cached);
        } else {
            AsyncTextureRequest request{};
            request.requestId = AllocateAsyncRequestId();
            if (request.requestId == 0) {
                return 0;
            }
            request.textureId = cached->second;
            request.completed = true;
            try {
                asyncRequests_.push_back(std::move(request));
            } catch (...) {
                return 0;
            }
            const uint32_t requestId = asyncRequests_.back().requestId;
            PruneCompletedAsyncRequests();
            return requestId;
        }
    }

    AsyncTextureRequest request{};
    request.requestId = AllocateAsyncRequestId();
    if (request.requestId == 0) {
        return 0;
    }
    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec) ||
        !TextureLimits::IsFileWithinInputBudget(resolvedPath)) {
        request.failed = true;
    } else {
        request.filePath = resolvedPath.wstring();
    }
    try {
        asyncRequests_.push_back(std::move(request));
    } catch (...) {
        return 0;
    }
    StartQueuedAsyncLoads();
    return asyncRequests_.back().requestId;
}

std::vector<uint32_t> TextureManager::RequestAsyncLoadBatch(
    const std::vector<std::wstring> &filePaths) {
    std::vector<uint32_t> requestIds;
    try {
        requestIds.reserve(filePaths.size());
    } catch (...) {
        return requestIds;
    }
    for (const std::wstring &filePath : filePaths) {
        requestIds.push_back(RequestAsyncLoad(filePath));
    }
    return requestIds;
}

void TextureManager::UpdateAsyncLoads() {
    if (!dxCommon_ || !dxCommon_->IsCommandListRecording()) {
        return;
    }

    StartQueuedAsyncLoads();

    for (AsyncTextureRequest &request : asyncRequests_) {
        if (request.completed || request.failed || request.job == nullptr) {
            continue;
        }

        if (!request.job->ready.load(std::memory_order_acquire)) {
            continue;
        }

        if (request.worker.joinable()) {
            request.worker.join();
        }
        DecodedTexture decoded = std::move(request.job->decoded);
        request.job.reset();
        if (!decoded.succeeded) {
            request.failed = true;
            request.filePath.clear();
            continue;
        }

        auto cached = filePathToTextureId_.find(decoded.pathKey);
        if (cached != filePathToTextureId_.end() &&
            IsValidTextureId(cached->second) &&
            cached->second != whiteTextureId_) {
            request.textureId = cached->second;
        } else {
            const bool hadPreviousCache = cached != filePathToTextureId_.end();
            const uint32_t previousCachedTextureId =
                hadPreviousCache ? cached->second : UINT32_MAX;
            try {
                if (cached == filePathToTextureId_.end()) {
                    cached = filePathToTextureId_.try_emplace(decoded.pathKey,
                                                              UINT32_MAX)
                                 .first;
                } else {
                    cached->second = UINT32_MAX;
                }

                const auto restoreCache = [&]() {
                    auto cacheIt = filePathToTextureId_.find(decoded.pathKey);
                    if (cacheIt == filePathToTextureId_.end()) {
                        return;
                    }
                    if (hadPreviousCache) {
                        cacheIt->second = previousCachedTextureId;
                    } else {
                        filePathToTextureId_.erase(cacheIt);
                    }
                };

                if (!dxCommon_->ReserveFrameRollbacks(2)) {
                    restoreCache();
                    request.failed = true;
                    continue;
                }

                const uint32_t requestId = request.requestId;
                const std::wstring pathKey = decoded.pathKey;
                std::function<void()> rollbackRequest =
                    [this, requestId, pathKey, hadPreviousCache,
                     previousCachedTextureId]() {
                        auto cacheIt = filePathToTextureId_.find(pathKey);
                        if (cacheIt != filePathToTextureId_.end()) {
                            if (hadPreviousCache) {
                                cacheIt->second = previousCachedTextureId;
                            } else {
                                filePathToTextureId_.erase(cacheIt);
                            }
                        }

                        const auto requestIt = std::find_if(
                            asyncRequests_.begin(), asyncRequests_.end(),
                            [requestId](const AsyncTextureRequest &request) {
                                return request.requestId == requestId;
                            });
                        if (requestIt != asyncRequests_.end()) {
                            requestIt->textureId = UINT32_MAX;
                            requestIt->completed = false;
                            requestIt->failed = true;
                        }
                    };

                if (!dxCommon_->RegisterFrameRollback(
                        this, std::move(rollbackRequest))) {
                    restoreCache();
                    request.textureId = UINT32_MAX;
                    request.failed = true;
                    continue;
                }

                request.textureId =
                    CreateTexture(decoded.scratch.GetImages(),
                                  decoded.scratch.GetImageCount(),
                                  decoded.metadata);
                if (!IsValidTextureId(request.textureId) ||
                    request.textureId == whiteTextureId_) {
                    restoreCache();
                    request.failed = true;
                    continue;
                }
                cached->second = request.textureId;
            } catch (...) {
                auto cacheIt = filePathToTextureId_.find(decoded.pathKey);
                if (cacheIt == filePathToTextureId_.end()) {
                    request.textureId = UINT32_MAX;
                    request.failed = true;
                    continue;
                }
                if (hadPreviousCache) {
                    cacheIt->second = previousCachedTextureId;
                } else {
                    filePathToTextureId_.erase(cacheIt);
                }
                request.textureId = UINT32_MAX;
                request.failed = true;
                continue;
            }
        }
        request.filePath.clear();
        request.completed = true;
    }

    PruneCompletedAsyncRequests();
    StartQueuedAsyncLoads();
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

void TextureManager::StartQueuedAsyncLoads() {
    size_t inFlightCount = 0;
    for (const AsyncTextureRequest &request : asyncRequests_) {
        if (!request.completed && !request.failed && request.job != nullptr) {
            ++inFlightCount;
        }
    }

    for (AsyncTextureRequest &request : asyncRequests_) {
        if (inFlightCount >= kMaxInFlightAsyncLoads) {
            return;
        }
        if (request.completed || request.failed || request.job != nullptr ||
            request.filePath.empty()) {
            continue;
        }

        const std::filesystem::path resolvedPath(request.filePath);
        std::shared_ptr<AsyncTextureJob> job;
        try {
            job = std::make_shared<AsyncTextureJob>();
            request.worker = std::thread([resolvedPath, job]() {
                try {
                    DecodedTexture decoded =
                        DecodeResolvedTextureFileForAsync(resolvedPath);
                    job->decoded = std::move(decoded);
                } catch (...) {
                    job->decoded = {};
                }
                job->ready.store(true, std::memory_order_release);
            });
        } catch (...) {
            request.failed = true;
            request.filePath.clear();
            continue;
        }
        request.job = std::move(job);
        ++inFlightCount;
    }
}

void TextureManager::StopAsyncLoads() {
    for (AsyncTextureRequest &request : asyncRequests_) {
        if (!request.worker.joinable()) {
            continue;
        }
        request.worker.join();
    }
    asyncRequests_.clear();
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

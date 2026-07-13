#include "core/PathUtils.h"
#include "graphics/DirectXCommon.h"
#include "internal/TextureManagerDecoding.h"
#include "internal/TextureManagerInternal.h"
#include "texture/TextureLimits.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

static constexpr size_t kMaxCompletedAsyncRequestHistory = 256;
static constexpr size_t kMaxTerminalAsyncRequestHistory = 1024;
static constexpr size_t kMaxInFlightAsyncLoads = 4;
static constexpr size_t kMaxPendingAsyncLoads = 64;

namespace {

bool IsActiveAsyncRequest(const TextureManagerAsyncRequest& request) {
    return !request.completed && !request.failed;
}

size_t CountActiveAsyncRequests(const std::vector<TextureManagerAsyncRequest>& requests) {
    return static_cast<size_t>(
        std::count_if(requests.begin(), requests.end(), IsActiveAsyncRequest));
}

} // namespace

void TextureManager::RecordAsyncTerminalState(uint32_t requestId, std::optional<uint32_t> textureId,
                                              bool failed) {
    if (requestId == 0) {
        return;
    }

    auto& terminalStates = state_->asyncState->terminalStates;
    const auto existing = std::find_if(terminalStates.begin(), terminalStates.end(),
                                       [requestId](const TextureManagerAsyncTerminalState& state) {
                                           return state.requestId == requestId;
                                       });
    if (existing != terminalStates.end()) {
        existing->textureId = textureId;
        existing->failed = failed;
        return;
    }

    try {
        terminalStates.push_back(TextureManagerAsyncTerminalState{requestId, textureId, failed});
    } catch (...) {
        return;
    }

    while (terminalStates.size() > kMaxTerminalAsyncRequestHistory) {
        terminalStates.pop_front();
    }
}

std::optional<TextureManagerAsyncTerminalState> TextureManager::FindAsyncTerminalState(
    uint32_t requestId) const {
    const auto& terminalStates = state_->asyncState->terminalStates;
    const auto it = std::find_if(terminalStates.begin(), terminalStates.end(),
                                 [requestId](const TextureManagerAsyncTerminalState& state) {
                                     return state.requestId == requestId;
                                 });
    if (it == terminalStates.end()) {
        return std::nullopt;
    }
    return *it;
}

uint32_t TextureManager::RequestAsyncLoad(const std::wstring& filePath) {
    if (!CanRequestAsyncLoad()) {
        return 0;
    }

    PruneCompletedAsyncRequests();

    std::filesystem::path resolvedPath;
    std::wstring pathKey;
    if (!TryResolveAsyncLoadPath(filePath, resolvedPath, pathKey)) {
        return 0;
    }

    if (std::optional<uint32_t> reusable = FindReusableAsyncRequestOrTexture(pathKey)) {
        return *reusable;
    }
    if (CountActiveAsyncRequests(state_->asyncState->requests) >= kMaxPendingAsyncLoads) {
        return 0;
    }

    TextureManagerAsyncRequest request{};
    if (!InitializeAsyncLoadRequest(resolvedPath, pathKey, request)) {
        return 0;
    }

    const uint32_t requestId = StoreAsyncLoadRequest(std::move(request));
    StartQueuedAsyncLoads();
    return requestId;
}

bool TextureManager::CanRequestAsyncLoad() const {
    return dxCommon_ != nullptr && dxCommon_->GetDevice() != nullptr && srvManager_ != nullptr &&
           IsValidTextureId(state_->whiteTextureId);
}

bool TextureManager::TryResolveAsyncLoadPath(const std::wstring& filePath,
                                             std::filesystem::path& resolvedPath,
                                             std::wstring& pathKey) {
    try {
        resolvedPath = PathUtils::ResolveAssetPath(filePath);
        pathKey = PathUtils::NormalizePathKey(resolvedPath);
    } catch (...) {
        return false;
    }
    return true;
}

std::optional<uint32_t> TextureManager::FindReusableAsyncRequestOrTexture(
    const std::wstring& pathKey) {
    const auto activeDuplicate =
        std::find_if(state_->asyncState->requests.begin(), state_->asyncState->requests.end(),
                     [&pathKey](const TextureManagerAsyncRequest& request) {
                         return IsActiveAsyncRequest(request) && request.pathKey == pathKey;
                     });
    if (activeDuplicate != state_->asyncState->requests.end()) {
        return activeDuplicate->requestId;
    }

    auto cached = state_->filePathToTextureId.find(pathKey);
    if (cached == state_->filePathToTextureId.end()) {
        return std::nullopt;
    }
    if (!IsValidTextureId(cached->second) || cached->second == state_->whiteTextureId) {
        state_->filePathToTextureId.erase(cached);
        return std::nullopt;
    }
    return EnqueueCompletedAsyncRequest(cached->second);
}

bool TextureManager::InitializeAsyncLoadRequest(const std::filesystem::path& resolvedPath,
                                                const std::wstring& pathKey,
                                                TextureManagerAsyncRequest& request) {
    request.requestId = AllocateAsyncRequestId();
    if (request.requestId == 0) {
        return false;
    }
    request.pathKey = pathKey;

    std::error_code ec;
    bool fileAvailable = false;
    try {
        fileAvailable = std::filesystem::exists(resolvedPath, ec) && !ec &&
                        TextureLimits::IsFileWithinInputBudget(resolvedPath);
    } catch (...) {
        fileAvailable = false;
    }
    if (!fileAvailable) {
        request.failed = true;
        return true;
    }

    try {
        request.filePath = resolvedPath.wstring();
    } catch (...) {
        request.failed = true;
    }
    return true;
}

uint32_t TextureManager::StoreAsyncLoadRequest(TextureManagerAsyncRequest&& request) {
    try {
        state_->asyncState->requests.push_back(std::move(request));
    } catch (...) {
        return 0;
    }

    const uint32_t requestId = state_->asyncState->requests.back().requestId;
    if (state_->asyncState->requests.back().failed) {
        RecordAsyncTerminalState(requestId, std::nullopt, true);
    }
    return requestId;
}

std::vector<uint32_t> TextureManager::RequestAsyncLoadBatch(
    const std::vector<std::wstring>& filePaths) {
    std::vector<uint32_t> requestIds;
    try {
        requestIds.reserve(filePaths.size());
    } catch (...) {
        return requestIds;
    }
    for (const std::wstring& filePath : filePaths) {
        try {
            requestIds.push_back(RequestAsyncLoad(filePath));
        } catch (...) {
            requestIds.push_back(0);
        }
    }
    return requestIds;
}

uint32_t TextureManager::EnqueueCompletedAsyncRequest(uint32_t textureId) {
    TextureManagerAsyncRequest request{};
    request.requestId = AllocateAsyncRequestId();
    if (request.requestId == 0) {
        return 0;
    }
    request.textureId = textureId;
    request.completed = true;
    try {
        state_->asyncState->requests.push_back(std::move(request));
    } catch (...) {
        return 0;
    }
    const uint32_t requestId = state_->asyncState->requests.back().requestId;
    RecordAsyncTerminalState(requestId, textureId, false);
    PruneCompletedAsyncRequests();
    return requestId;
}

void TextureManager::RestoreAsyncTextureCache(const std::wstring& pathKey, bool hadPreviousCache,
                                              uint32_t previousTextureId) {
    auto cacheIt = state_->filePathToTextureId.find(pathKey);
    if (cacheIt == state_->filePathToTextureId.end()) {
        return;
    }
    if (hadPreviousCache) {
        cacheIt->second = previousTextureId;
    } else {
        state_->filePathToTextureId.erase(cacheIt);
    }
}

bool TextureManager::TryCompleteAsyncTextureUpload(TextureManagerAsyncRequest& request,
                                                   TextureManagerDecodedTexture& decoded) {
    auto cached = state_->filePathToTextureId.find(decoded.pathKey);
    if (cached != state_->filePathToTextureId.end() && IsValidTextureId(cached->second) &&
        cached->second != state_->whiteTextureId) {
        request.textureId = cached->second;
        return true;
    }

    const bool hadPreviousCache = cached != state_->filePathToTextureId.end();
    const uint32_t previousCachedTextureId = hadPreviousCache ? cached->second : kInvalidResourceId;
    std::function<void()> manualRollbackRequest;
    try {
        if (cached == state_->filePathToTextureId.end()) {
            cached =
                state_->filePathToTextureId.try_emplace(decoded.pathKey, kInvalidResourceId).first;
        } else {
            cached->second = kInvalidResourceId;
        }

        if (!dxCommon_->ReserveFrameRollbacks(2)) {
            RestoreAsyncTextureCache(decoded.pathKey, hadPreviousCache, previousCachedTextureId);
            return false;
        }

        const uint32_t requestId = request.requestId;
        const std::wstring pathKey = decoded.pathKey;
        auto rollbackArmed = std::make_shared<bool>(true);
        std::function<void()> rollbackRequest = [this, requestId, pathKey, hadPreviousCache,
                                                 previousCachedTextureId, rollbackArmed]() {
            if (!*rollbackArmed) {
                return;
            }
            *rollbackArmed = false;
            RestoreAsyncTextureCache(pathKey, hadPreviousCache, previousCachedTextureId);

            const auto requestIt = std::find_if(
                state_->asyncState->requests.begin(), state_->asyncState->requests.end(),
                [requestId](const TextureManagerAsyncRequest& request) {
                    return request.requestId == requestId;
                });
            if (requestIt != state_->asyncState->requests.end()) {
                requestIt->textureId = kInvalidResourceId;
                requestIt->completed = false;
                requestIt->failed = true;
            }
            RecordAsyncTerminalState(requestId, std::nullopt, true);
        };
        manualRollbackRequest = rollbackRequest;

        if (!dxCommon_->RegisterFrameRollback(this, std::move(rollbackRequest))) {
            manualRollbackRequest();
            request.textureId = kInvalidResourceId;
            return false;
        }

        request.textureId = CreateTexture(decoded.scratch.GetImages(),
                                          decoded.scratch.GetImageCount(), decoded.metadata);
        if (!IsValidTextureId(request.textureId) || request.textureId == state_->whiteTextureId) {
            manualRollbackRequest();
            request.textureId = kInvalidResourceId;
            return false;
        }
        cached->second = request.textureId;
        return true;
    } catch (...) {
        if (manualRollbackRequest) {
            manualRollbackRequest();
        } else {
            RestoreAsyncTextureCache(decoded.pathKey, hadPreviousCache, previousCachedTextureId);
        }
        request.textureId = kInvalidResourceId;
        return false;
    }
}

void TextureManager::UpdateAsyncLoads() {
    if (!dxCommon_ || !dxCommon_->IsCommandListRecording()) {
        return;
    }

    StartQueuedAsyncLoads();

    for (TextureManagerAsyncRequest& request : state_->asyncState->requests) {
        if (request.completed || request.failed || request.job == nullptr) {
            continue;
        }

        if (!request.job->ready.load(std::memory_order_acquire)) {
            continue;
        }

        TextureManagerDecodedTexture decoded = std::move(request.job->decoded);
        request.job.reset();
        if (!decoded.succeeded) {
            request.failed = true;
            RecordAsyncTerminalState(request.requestId, std::nullopt, true);
            request.filePath.clear();
            request.pathKey.clear();
            continue;
        }

        if (!TryCompleteAsyncTextureUpload(request, decoded)) {
            request.failed = true;
            RecordAsyncTerminalState(request.requestId, std::nullopt, true);
            request.filePath.clear();
            request.pathKey.clear();
            continue;
        }
        request.filePath.clear();
        request.pathKey.clear();
        request.completed = true;
        RecordAsyncTerminalState(request.requestId, request.textureId, false);
    }

    PruneCompletedAsyncRequests();
    StartQueuedAsyncLoads();
}

bool TextureManager::IsAsyncLoadComplete(uint32_t requestId) const {
    const auto it =
        std::find_if(state_->asyncState->requests.begin(), state_->asyncState->requests.end(),
                     [requestId](const TextureManagerAsyncRequest& request) {
                         return request.requestId == requestId;
                     });
    if (it != state_->asyncState->requests.end()) {
        return it->completed;
    }
    const std::optional<TextureManagerAsyncTerminalState> terminal =
        FindAsyncTerminalState(requestId);
    return terminal.has_value() && terminal->textureId.has_value() && !terminal->failed;
}

std::optional<uint32_t> TextureManager::GetAsyncTextureId(uint32_t requestId) const {
    const auto it =
        std::find_if(state_->asyncState->requests.begin(), state_->asyncState->requests.end(),
                     [requestId](const TextureManagerAsyncRequest& request) {
                         return request.requestId == requestId && request.completed;
                     });
    if (it != state_->asyncState->requests.end()) {
        return it->textureId;
    }
    const std::optional<TextureManagerAsyncTerminalState> terminal =
        FindAsyncTerminalState(requestId);
    return terminal ? terminal->textureId : std::nullopt;
}

bool TextureManager::HasAsyncLoadFailed(uint32_t requestId) const {
    const auto it =
        std::find_if(state_->asyncState->requests.begin(), state_->asyncState->requests.end(),
                     [requestId](const TextureManagerAsyncRequest& request) {
                         return request.requestId == requestId;
                     });
    if (it != state_->asyncState->requests.end()) {
        return it->failed;
    }
    const std::optional<TextureManagerAsyncTerminalState> terminal =
        FindAsyncTerminalState(requestId);
    return terminal.has_value() && terminal->failed;
}

void TextureManager::PruneCompletedAsyncRequests() {
    const size_t completedCount = static_cast<size_t>(
        std::count_if(state_->asyncState->requests.begin(), state_->asyncState->requests.end(),
                      [](const TextureManagerAsyncRequest& request) {
                          return request.completed || request.failed;
                      }));
    if (completedCount <= kMaxCompletedAsyncRequestHistory) {
        return;
    }

    size_t removeCount = completedCount - kMaxCompletedAsyncRequestHistory;
    state_->asyncState->requests.erase(
        std::remove_if(state_->asyncState->requests.begin(), state_->asyncState->requests.end(),
                       [&removeCount](const TextureManagerAsyncRequest& request) {
                           if (removeCount == 0 || IsActiveAsyncRequest(request)) {
                               return false;
                           }
                           --removeCount;
                           return true;
                       }),
        state_->asyncState->requests.end());
}

void TextureManager::EnsureAsyncWorkers() {
    if (state_->asyncState->workerPool.IsRunning()) {
        return;
    }

    if (!state_->asyncState->workerPool.Start(
            kMaxInFlightAsyncLoads, [](TextureManagerAsyncWorkItem& item) {
                try {
                    item.job->decoded =
                        TextureManagerDecoding::DecodeResolvedFileForAsync(item.filePath);
                } catch (...) {
                    item.job->decoded = {};
                }
                item.job->ready.store(true, std::memory_order_release);
            })) {
        for (TextureManagerAsyncRequest& request : state_->asyncState->requests) {
            if (IsActiveAsyncRequest(request) && !request.queued) {
                request.failed = true;
                RecordAsyncTerminalState(request.requestId, std::nullopt, true);
                request.filePath.clear();
                request.pathKey.clear();
            }
        }
    }
}

void TextureManager::StartQueuedAsyncLoads() {
    EnsureAsyncWorkers();
    if (!state_->asyncState->workerPool.IsRunning()) {
        for (TextureManagerAsyncRequest& request : state_->asyncState->requests) {
            if (IsActiveAsyncRequest(request) && !request.queued) {
                request.failed = true;
                RecordAsyncTerminalState(request.requestId, std::nullopt, true);
                request.filePath.clear();
                request.pathKey.clear();
            }
        }
        return;
    }

    for (TextureManagerAsyncRequest& request : state_->asyncState->requests) {
        if (!IsActiveAsyncRequest(request) || request.queued || request.filePath.empty()) {
            continue;
        }

        std::shared_ptr<TextureManagerAsyncJob> job;
        try {
            job = std::make_shared<TextureManagerAsyncJob>();
        } catch (...) {
            request.failed = true;
            RecordAsyncTerminalState(request.requestId, std::nullopt, true);
            request.filePath.clear();
            request.pathKey.clear();
            continue;
        }
        request.job = std::move(job);
        if (!state_->asyncState->workerPool.Enqueue(
                TextureManagerAsyncWorkItem{request.requestId, request.filePath, request.job})) {
            request.job.reset();
            request.failed = true;
            RecordAsyncTerminalState(request.requestId, std::nullopt, true);
            request.filePath.clear();
            request.pathKey.clear();
            continue;
        }
        request.queued = true;
    }
}

void TextureManager::StopAsyncWorkers() {
    state_->asyncState->StopWorkers();
}

void TextureManager::StopAsyncLoads() {
    StopAsyncWorkers();
    state_->asyncState->requests.clear();
    state_->asyncState->terminalStates.clear();
    state_->asyncState->workerPool.ClearPending();
}

uint32_t TextureManager::AllocateAsyncRequestId() {
    if (state_->asyncState->requests.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)()) - 1u) {
        return 0;
    }

    for (;;) {
        if (state_->asyncState->nextRequestId == 0) {
            state_->asyncState->nextRequestId = 1;
        }
        const uint32_t candidate = state_->asyncState->nextRequestId++;
        const auto it =
            std::find_if(state_->asyncState->requests.begin(), state_->asyncState->requests.end(),
                         [candidate](const TextureManagerAsyncRequest& request) {
                             return request.requestId == candidate;
                         });
        if (it == state_->asyncState->requests.end()) {
            return candidate;
        }
    }
}

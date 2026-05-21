#include "texture/TextureManager.h"
#include "core/AssetManager.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include "graphics/SrvManager.h"
#include "texture/Texture.h"
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <future>
#include <stdexcept>
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
    if (!std::filesystem::exists(resolvedPath)) {
        throw std::runtime_error("Texture file not found. requested=" +
                                 std::filesystem::path(filePath).string() +
                                 " resolved=" + resolvedPath.string());
    }

    TextureManager::DecodedTexture decoded{};
    decoded.pathKey = NormalizePathKey(resolvedPath);
    const std::wstring ext = resolvedPath.extension().wstring();

    if (_wcsicmp(ext.c_str(), L".dds") == 0) {
        const std::string message =
            "LoadFromDDSFile failed: " + resolvedPath.string();
        DxUtils::ThrowIfFailed(
            DirectX::LoadFromDDSFile(resolvedPath.c_str(),
                                      DirectX::DDS_FLAGS_NONE,
                                      &decoded.metadata, decoded.scratch),
            message.c_str());
    } else {
        const std::string message =
            "LoadFromWICFile failed: " + resolvedPath.string();
        DxUtils::ThrowIfFailed(
            DirectX::LoadFromWICFile(resolvedPath.c_str(),
                                      DirectX::WIC_FLAGS_IGNORE_SRGB,
                                      &decoded.metadata, decoded.scratch),
            message.c_str());
    }

    return decoded;
}

using namespace DirectX;
using namespace DxUtils;
using Microsoft::WRL::ComPtr;



uint32_t TextureManager::RequestAsyncLoad(const std::wstring &filePath) {
    const std::filesystem::path resolvedPath = ResolveTexturePath(filePath);
    const std::wstring pathKey = NormalizePathKey(resolvedPath);
    if (filePathToTextureId_.find(pathKey) != filePathToTextureId_.end()) {
        AsyncTextureRequest request{};
        request.requestId = nextAsyncRequestId_++;
        request.textureId = filePathToTextureId_[pathKey];
        request.completed = true;
        asyncRequests_.push_back(std::move(request));
        return asyncRequests_.back().requestId;
    }

    AsyncTextureRequest request{};
    request.requestId = nextAsyncRequestId_++;
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

        try {
            DecodedTexture decoded = request.future.get();
            auto cached = filePathToTextureId_.find(decoded.pathKey);
            if (cached != filePathToTextureId_.end()) {
                request.textureId = cached->second;
            } else {
                request.textureId = CreateTexture(
                    decoded.scratch.GetImages(), decoded.scratch.GetImageCount(),
                    decoded.metadata);
                filePathToTextureId_[decoded.pathKey] = request.textureId;
            }
            request.completed = true;
        } catch (...) {
            request.failed = true;
        }
    }
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

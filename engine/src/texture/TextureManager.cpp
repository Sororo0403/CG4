#include "texture/TextureManager.h"

#include "core/PathUtils.h"
#include "graphics/DirectXCommon.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"
#include "internal/TextureManagerDecoding.h"
#include "internal/TextureManagerInternal.h"
#include "texture/Texture.h"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <iterator>
#include <numeric>
#include <string_view>
#include <vector>

class TextureManagerInitializationGuard {
public:
    explicit TextureManagerInitializationGuard(TextureManager& manager) : manager_(manager) {}
    ~TextureManagerInitializationGuard() {
        if (active_) {
            manager_.Finalize();
        }
    }

    TextureManagerInitializationGuard(const TextureManagerInitializationGuard&) = delete;
    TextureManagerInitializationGuard& operator=(const TextureManagerInitializationGuard&) = delete;

    void Commit() {
        active_ = false;
    }

private:
    TextureManager& manager_;
    bool active_ = true;
};

using namespace DirectX;

namespace {
uint64_t BufferByteWidth(ID3D12Resource* resource) {
    if (resource == nullptr) {
        return 0;
    }
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    return desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? desc.Width : 0;
}

TextureManagerDecoding::TextureColorSpacePolicy DecodeColorSpacePolicy(int policy) {
    struct ColorSpacePolicyMap {
        int value;
        TextureManagerDecoding::TextureColorSpacePolicy policy;
    };
    static constexpr std::array<ColorSpacePolicyMap, 2> kPolicies{{
        {1, TextureManagerDecoding::TextureColorSpacePolicy::Srgb},
        {2, TextureManagerDecoding::TextureColorSpacePolicy::Linear},
    }};

    const auto it =
        std::find_if(kPolicies.begin(), kPolicies.end(),
                     [policy](const ColorSpacePolicyMap& entry) { return entry.value == policy; });
    return it != kPolicies.end() ? it->policy
                                 : TextureManagerDecoding::TextureColorSpacePolicy::Auto;
}

std::wstring MakeTextureCacheKey(const std::wstring& pathKey, int colorSpacePolicy) {
    struct CacheKeySuffix {
        int policy = 0;
        std::wstring_view suffix;
    };
    static constexpr std::array<CacheKeySuffix, 2> kSuffixes{{
        {1, L"|srgb"},
        {2, L"|linear"},
    }};

    const auto it = std::find_if(kSuffixes.begin(), kSuffixes.end(),
                                 [colorSpacePolicy](const CacheKeySuffix& entry) {
                                     return entry.policy == colorSpacePolicy;
                                 });
    try {
        return it != kSuffixes.end() ? pathKey + std::wstring(it->suffix) : pathKey;
    } catch (const std::exception&) {
        return {};
    }
}

} // namespace

TextureManager::TextureManager() : state_(std::make_unique<State>()) {}

TextureManager::~TextureManager() {
    Finalize(true);
}

uint32_t TextureManager::GetWhiteTextureId() const {
    return state_->whiteTextureId;
}

uint32_t TextureManager::GetWhiteCubeTextureId() const {
    return state_->whiteCubeTextureId;
}

uint32_t TextureManager::GetBlackCubeTextureId() const {
    return state_->blackCubeTextureId;
}

uint32_t TextureManager::GetDefaultNormalTextureId() const {
    return state_->defaultNormalTextureId;
}

void TextureManager::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager) {
    if (!dxCommon || !srvManager) {
        Finalize();
        return;
    }
    if (!Finalize()) {
        return;
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    TextureManagerInitializationGuard initializeGuard(*this);

    if (!ResetStateForInitialize()) {
        return;
    }
    if (!CreateDefaultTextures()) {
        return;
    }
    initializeGuard.Commit();
}

bool TextureManager::ResetStateForInitialize() {
    state_->textures.clear();
    state_->uploadBuffers.clear();
    state_->frameUploadBuffers.clear();
    try {
        state_->frameUploadBuffers.resize(dxCommon_->GetSwapChainBufferCount());
    } catch (const std::exception&) {
        return false;
    }
    state_->filePathToTextureId.clear();
    state_->asyncState->Reset();
    state_->lastDynamicUploadFrameIndex = UINT_MAX;
    return true;
}

bool TextureManager::CreateDefaultTextures() {
    uint32_t whitePixel = 0xFFFFFFFF;
    Image image{};
    image.width = 1;
    image.height = 1;
    image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    image.rowPitch = sizeof(uint32_t);
    image.slicePitch = sizeof(uint32_t);
    image.pixels = reinterpret_cast<uint8_t*>(&whitePixel);

    TexMetadata metadata{};
    metadata.width = 1;
    metadata.height = 1;
    metadata.depth = 1;
    metadata.arraySize = 1;
    metadata.mipLevels = 1;
    metadata.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    metadata.dimension = TEX_DIMENSION_TEXTURE2D;

    state_->whiteTextureId = CreateTexture(&image, 1, metadata);

    Image cubeImages[6]{};
    std::fill(std::begin(cubeImages), std::end(cubeImages), image);
    TexMetadata cubeMetadata = metadata;
    cubeMetadata.arraySize = 6;
    cubeMetadata.miscFlags = TEX_MISC_TEXTURECUBE;
    state_->whiteCubeTextureId = CreateTexture(cubeImages, _countof(cubeImages), cubeMetadata);

    uint32_t blackPixel = 0xFF000000;
    image.pixels = reinterpret_cast<uint8_t*>(&blackPixel);
    Image blackCubeImages[6]{};
    std::fill(std::begin(blackCubeImages), std::end(blackCubeImages), image);
    state_->blackCubeTextureId =
        CreateTexture(blackCubeImages, _countof(blackCubeImages), cubeMetadata);

    uint32_t flatNormalPixel = 0xFFFF8080;
    image.pixels = reinterpret_cast<uint8_t*>(&flatNormalPixel);
    state_->defaultNormalTextureId = CreateTexture(&image, 1, metadata);
    return HasValidDefaultTextures();
}

bool TextureManager::HasExpectedDefaultTexture(uint32_t textureId, UINT16 arraySize,
                                               bool isCube) const {
    if (!IsValidTextureId(textureId)) {
        return false;
    }
    const Texture& texture = state_->textures[textureId].texture;
    if (texture.arraySize != arraySize || texture.isCube != isCube) {
        return false;
    }
    ID3D12Resource* resource = texture.resource.Get();
    if (resource == nullptr) {
        return false;
    }
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    return desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.Width == 1 &&
           desc.Height == 1 && desc.DepthOrArraySize == arraySize && desc.MipLevels == 1 &&
           desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM;
}

bool TextureManager::HasValidDefaultTextures() const {
    return HasExpectedDefaultTexture(state_->whiteTextureId, 1, false) &&
           HasExpectedDefaultTexture(state_->whiteCubeTextureId, 6, true) &&
           HasExpectedDefaultTexture(state_->blackCubeTextureId, 6, true) &&
           HasExpectedDefaultTexture(state_->defaultNormalTextureId, 1, false) &&
           state_->whiteCubeTextureId != state_->whiteTextureId &&
           state_->blackCubeTextureId != state_->whiteTextureId &&
           state_->defaultNormalTextureId != state_->whiteTextureId &&
           state_->blackCubeTextureId != state_->whiteCubeTextureId;
}

uint32_t TextureManager::GetWhiteFallbackTextureId() const {
    return IsValidTextureId(state_->whiteTextureId) ? state_->whiteTextureId : kInvalidResourceId;
}

bool TextureManager::ReleaseTexture(uint32_t textureId, bool allowFrameAbort) {
    if (!IsValidTextureId(textureId)) {
        return true;
    }
    if (textureId == state_->whiteTextureId || textureId == state_->whiteCubeTextureId ||
        textureId == state_->blackCubeTextureId || textureId == state_->defaultNormalTextureId) {
        return false;
    }
    if (!CanReleaseGpuResources(dxCommon_, true, allowFrameAbort)) {
        return false;
    }

    Entry& entry = state_->textures[textureId];
    if (srvManager_ != nullptr) {
        srvManager_->FreeIfAllocated(entry.srvIndex);
    }
    entry = {};

    for (auto it = state_->filePathToTextureId.begin(); it != state_->filePathToTextureId.end();) {
        if (it->second == textureId) {
            it = state_->filePathToTextureId.erase(it);
        } else {
            ++it;
        }
    }
    return true;
}

bool TextureManager::Finalize() {
    return Finalize(false);
}

bool TextureManager::Finalize(bool allowFrameAbort) {
    const bool hasFrameUploadBuffers =
        std::any_of(state_->frameUploadBuffers.begin(), state_->frameUploadBuffers.end(),
                    [](const auto& buffers) { return !buffers.empty(); });
    const bool hasGpuResources =
        !state_->textures.empty() || !state_->uploadBuffers.empty() || hasFrameUploadBuffers;
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources, allowFrameAbort)) {
        return false;
    }

    StopAsyncLoads();
    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }
    ReleaseUploadBuffers();

    if (srvManager_ != nullptr) {
        for (const Entry& entry : state_->textures) {
            srvManager_->FreeIfAllocated(entry.srvIndex);
        }
    }

    state_->textures.clear();
    state_->uploadBuffers.clear();
    state_->frameUploadBuffers.clear();
    state_->filePathToTextureId.clear();
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    state_->whiteTextureId = kInvalidResourceId;
    state_->whiteCubeTextureId = kInvalidResourceId;
    state_->blackCubeTextureId = kInvalidResourceId;
    state_->defaultNormalTextureId = kInvalidResourceId;
    state_->lastDynamicUploadFrameIndex = UINT_MAX;
    return true;
}

uint32_t TextureManager::Load(const std::wstring& filePath) {
    return LoadWithColorSpace(filePath, 0);
}

uint32_t TextureManager::LoadSrgb(const std::wstring& filePath) {
    return LoadWithColorSpace(filePath, 1);
}

uint32_t TextureManager::LoadLinear(const std::wstring& filePath) {
    return LoadWithColorSpace(filePath, 2);
}

uint32_t TextureManager::LoadWithColorSpace(const std::wstring& filePath, int colorSpacePolicy) {
    std::filesystem::path resolvedPath;
    try {
        resolvedPath = PathUtils::ResolveAssetPath(filePath);
    } catch (const std::exception&) {
        return GetWhiteFallbackTextureId();
    }
    std::error_code ec;
    try {
        if (!std::filesystem::exists(resolvedPath, ec)) {
            return GetWhiteFallbackTextureId();
        }
    } catch (const std::exception&) {
        return GetWhiteFallbackTextureId();
    }

    std::wstring pathKey;
    try {
        pathKey = PathUtils::NormalizePathKey(resolvedPath);
    } catch (const std::exception&) {
        return GetWhiteFallbackTextureId();
    }
    const std::wstring cacheKey = MakeTextureCacheKey(pathKey, colorSpacePolicy);
    if (cacheKey.empty()) {
        return GetWhiteFallbackTextureId();
    }

    auto it = state_->filePathToTextureId.find(cacheKey);
    if (it != state_->filePathToTextureId.end()) {
        if (IsValidTextureId(it->second) && it->second != state_->whiteTextureId) {
            return it->second;
        }
        state_->filePathToTextureId.erase(it);
    }

    ScratchImage scratch;
    TexMetadata metadata{};

    if (!TextureManagerDecoding::DecodeFileForLoad(resolvedPath, scratch, metadata,
                                                   DecodeColorSpacePolicy(colorSpacePolicy))) {
        return GetWhiteFallbackTextureId();
    }

    uint32_t id = CreateTexture(scratch.GetImages(), scratch.GetImageCount(), metadata);
    if (IsValidTextureId(id) && id != state_->whiteTextureId) {
        try {
            state_->filePathToTextureId[cacheKey] = id;
        } catch (const std::exception&) {
        }
    }

    return id;
}

std::vector<uint32_t> TextureManager::LoadBatch(const std::vector<std::wstring>& filePaths) {
    std::vector<uint32_t> textureIds;
    try {
        textureIds.reserve(filePaths.size());
        std::transform(filePaths.begin(), filePaths.end(), std::back_inserter(textureIds),
                       [this](const std::wstring& filePath) { return Load(filePath); });
    } catch (const std::exception&) {
        textureIds.clear();
    }
    return textureIds;
}

uint32_t TextureManager::LoadFromMemory(const uint8_t* data, size_t size) {
    return LoadFromMemoryWithColorSpace(data, size, 0);
}

uint32_t TextureManager::LoadFromMemorySrgb(const uint8_t* data, size_t size) {
    return LoadFromMemoryWithColorSpace(data, size, 1);
}

uint32_t TextureManager::LoadFromMemoryLinear(const uint8_t* data, size_t size) {
    return LoadFromMemoryWithColorSpace(data, size, 2);
}

uint32_t TextureManager::LoadFromMemoryWithColorSpace(const uint8_t* data, size_t size,
                                                      int colorSpacePolicy) {
    if (!data || size == 0) {
        return GetWhiteFallbackTextureId();
    }

    ScratchImage scratch;
    TexMetadata metadata{};

    if (!TextureManagerDecoding::DecodeMemoryForLoad(data, size, scratch, metadata,
                                                     DecodeColorSpacePolicy(colorSpacePolicy))) {
        return GetWhiteFallbackTextureId();
    }

    uint32_t id = CreateTexture(scratch.GetImages(), scratch.GetImageCount(), metadata);

    return id;
}

D3D12_GPU_DESCRIPTOR_HANDLE
TextureManager::GetGpuHandle(uint32_t textureId) const {
    if (!IsValidTextureId(textureId) || srvManager_ == nullptr ||
        !srvManager_->IsAllocated(state_->textures[textureId].srvIndex)) {
        if (srvManager_ != nullptr && IsValidTextureId(state_->whiteTextureId) &&
            srvManager_->IsAllocated(state_->textures[state_->whiteTextureId].srvIndex)) {
            return srvManager_->GetGpuHandle(state_->textures[state_->whiteTextureId].srvIndex);
        }
        return {};
    }
    return srvManager_->GetGpuHandle(state_->textures[textureId].srvIndex);
}

bool TextureManager::IsValidTextureId(uint32_t textureId) const {
    return textureId < state_->textures.size() &&
           state_->textures[textureId].texture.resource != nullptr;
}

bool TextureManager::IsCubeTextureId(uint32_t textureId) const {
    return IsValidTextureId(textureId) && state_->textures[textureId].texture.isCube;
}

ID3D12Resource* TextureManager::GetResource(uint32_t textureId) const {
    if (!IsValidTextureId(textureId)) {
        return nullptr;
    }
    return state_->textures[textureId].texture.resource.Get();
}

uint32_t TextureManager::GetWidth(uint32_t id) const {
    if (!IsValidTextureId(id)) {
        return 0;
    }
    return state_->textures[id].texture.width;
}

uint32_t TextureManager::GetHeight(uint32_t id) const {
    if (!IsValidTextureId(id)) {
        return 0;
    }
    return state_->textures[id].texture.height;
}

size_t TextureManager::GetTextureCount() const {
    return static_cast<size_t>(
        std::count_if(state_->textures.begin(), state_->textures.end(),
                      [](const Entry& entry) { return entry.texture.resource != nullptr; }));
}

uint64_t TextureManager::GetTextureGpuBytes() const {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return 0;
    }

    return std::accumulate(state_->textures.begin(), state_->textures.end(), uint64_t{0},
                           [this](uint64_t bytes, const Entry& entry) {
                               ID3D12Resource* resource = entry.texture.resource.Get();
                               if (resource == nullptr) {
                                   return bytes;
                               }
                               const D3D12_RESOURCE_DESC desc = resource->GetDesc();
                               const D3D12_RESOURCE_ALLOCATION_INFO info =
                                   dxCommon_->GetDevice()->GetResourceAllocationInfo(0, 1, &desc);
                               return bytes + info.SizeInBytes;
                           });
}

uint64_t TextureManager::GetUploadBytes() const {
    const uint64_t uploadBytes = std::accumulate(
        state_->uploadBuffers.begin(), state_->uploadBuffers.end(), uint64_t{0},
        [](uint64_t bytes, const auto& buffer) { return bytes + BufferByteWidth(buffer.Get()); });
    return std::accumulate(state_->frameUploadBuffers.begin(), state_->frameUploadBuffers.end(),
                           uploadBytes, [](uint64_t bytes, const auto& buffers) {
                               return std::accumulate(buffers.begin(), buffers.end(), bytes,
                                                      [](uint64_t total, const auto& buffer) {
                                                          return total +
                                                                 BufferByteWidth(buffer.Get());
                                                      });
                           });
}

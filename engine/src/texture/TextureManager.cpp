#include "texture/TextureManager.h"
#include "core/ComInitialization.h"
#include "core/PathUtils.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"
#include "texture/Texture.h"
#include "texture/TextureLimits.h"
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

class UploadPassScope {
  public:
    UploadPassScope(DirectXCommon *dxCommon, TextureManager *textureManager,
                    bool active)
        : dxCommon_(dxCommon), textureManager_(textureManager), active_(active) {}

    ~UploadPassScope() {
        if (active_ && dxCommon_ != nullptr) {
            dxCommon_->AbortFrame();
            if (textureManager_ != nullptr) {
                textureManager_->ReleaseUploadBuffers();
            }
        }
    }

    bool Finish() {
        if (!active_) {
            return true;
        }
        const DirectXCommon::UploadPassResult result =
            dxCommon_->EndUploadPass();
        if (result == DirectXCommon::UploadPassResult::Failed) {
            return false;
        }
        if (result == DirectXCommon::UploadPassResult::Completed &&
            textureManager_ != nullptr) {
            textureManager_->ReleaseUploadBuffers();
        }
        active_ = false;
        return true;
    }

  private:
    DirectXCommon *dxCommon_ = nullptr;
    TextureManager *textureManager_ = nullptr;
    bool active_ = false;
};

class TextureManagerInitializationGuard {
  public:
    explicit TextureManagerInitializationGuard(TextureManager &manager)
        : manager_(manager) {}
    ~TextureManagerInitializationGuard() {
        if (active_) {
            manager_.Finalize();
        }
    }

    TextureManagerInitializationGuard(const TextureManagerInitializationGuard &) =
        delete;
    TextureManagerInitializationGuard &
    operator=(const TextureManagerInitializationGuard &) = delete;

    void Commit() { active_ = false; }

  private:
    TextureManager &manager_;
    bool active_ = true;
};

static bool DecodeTextureFileForLoad(const std::filesystem::path &resolvedPath,
                                     DirectX::ScratchImage &scratch,
                                     DirectX::TexMetadata &metadata) {
    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec) ||
        !TextureLimits::IsFileWithinInputBudget(resolvedPath)) {
        return false;
    }

    const std::wstring ext = resolvedPath.extension().wstring();
    const bool isDds = _wcsicmp(ext.c_str(), L".dds") == 0;

    if (isDds) {
        if (FAILED(DirectX::GetMetadataFromDDSFile(
                resolvedPath.c_str(), DirectX::DDS_FLAGS_NONE, metadata)) ||
            !TextureLimits::IsMetadataWithinBudget(metadata)) {
            return false;
        }
        if (FAILED(DirectX::LoadFromDDSFile(
                resolvedPath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata,
                scratch))) {
            return false;
        }
        return TextureLimits::IsMetadataWithinBudget(
                   metadata, scratch.GetImageCount()) &&
               TextureLimits::AreImagesWithinDecodedBudget(
                   scratch.GetImages(), scratch.GetImageCount());
    }

    ScopedComInitialization com;
    if (!com.IsUsable()) {
        return false;
    }
    if (FAILED(DirectX::GetMetadataFromWICFile(
            resolvedPath.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, metadata)) ||
        !TextureLimits::IsMetadataWithinBudget(metadata)) {
        return false;
    }
    if (FAILED(DirectX::LoadFromWICFile(
            resolvedPath.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, &metadata,
            scratch))) {
        return false;
    }
    return TextureLimits::IsMetadataWithinBudget(
               metadata, scratch.GetImageCount()) &&
           TextureLimits::AreImagesWithinDecodedBudget(
               scratch.GetImages(), scratch.GetImageCount());
}

static bool DecodeTextureMemoryForLoad(const uint8_t *data, size_t size,
                                       DirectX::ScratchImage &scratch,
                                       DirectX::TexMetadata &metadata) {
    if (!data || !TextureLimits::IsMemoryWithinInputBudget(size)) {
        return false;
    }

    ScopedComInitialization com;
    if (!com.IsUsable()) {
        return false;
    }
    if (FAILED(DirectX::GetMetadataFromWICMemory(
            data, size, DirectX::WIC_FLAGS_IGNORE_SRGB, metadata)) ||
        !TextureLimits::IsMetadataWithinBudget(metadata)) {
        return false;
    }
    if (FAILED(DirectX::LoadFromWICMemory(
            data, size, DirectX::WIC_FLAGS_IGNORE_SRGB, &metadata, scratch))) {
        return false;
    }
    return TextureLimits::IsMetadataWithinBudget(
               metadata, scratch.GetImageCount()) &&
           TextureLimits::AreImagesWithinDecodedBudget(
               scratch.GetImages(), scratch.GetImageCount());
}

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {
TextureManager *gActiveTextureManager = nullptr;

uint64_t BufferByteWidth(ID3D12Resource *resource) {
    if (resource == nullptr) {
        return 0;
    }
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    return desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? desc.Width : 0;
}

class ScopedSrvAllocation {
  public:
    explicit ScopedSrvAllocation(SrvManager *srvManager)
        : srvManager_(srvManager) {}
    ~ScopedSrvAllocation() {
        if (srvManager_ != nullptr && index_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(index_);
        }
    }

    uint32_t Allocate() {
        if (srvManager_ == nullptr || !srvManager_->CanAllocate()) {
            return UINT32_MAX;
        }
        index_ = srvManager_->Allocate();
        return index_;
    }

    void Commit() { index_ = UINT32_MAX; }

    ScopedSrvAllocation(const ScopedSrvAllocation &) = delete;
    ScopedSrvAllocation &operator=(const ScopedSrvAllocation &) = delete;

  private:
    SrvManager *srvManager_ = nullptr;
    uint32_t index_ = UINT32_MAX;
};
}

TextureManager &TextureManager::GetInstance() {
    static TextureManager instance;
    return gActiveTextureManager != nullptr ? *gActiveTextureManager : instance;
}

void TextureManager::SetActiveInstance(TextureManager *instance) {
    gActiveTextureManager = instance;
}

TextureManager::~TextureManager() {
    Finalize(true);
}

void TextureManager::Initialize(DirectXCommon *dxCommon,
                                SrvManager *srvManager) {
    if (!dxCommon || !srvManager) {
        Finalize();
        return;
    }
    if (!Finalize()) {
        return;
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    SetActiveInstance(this);
    TextureManagerInitializationGuard initializeGuard(*this);

    textures_.clear();
    uploadBuffers_.clear();
    frameUploadBuffers_.clear();
    try {
        frameUploadBuffers_.resize(dxCommon_->GetSwapChainBufferCount());
    } catch (...) {
        return;
    }
    filePathToTextureId_.clear();
    asyncRequests_.clear();
    nextAsyncRequestId_ = 1;
    lastDynamicUploadFrameIndex_ = UINT_MAX;

    uint32_t whitePixel = 0xFFFFFFFF;
    Image image{};
    image.width = 1;
    image.height = 1;
    image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    image.rowPitch = sizeof(uint32_t);
    image.slicePitch = sizeof(uint32_t);
    image.pixels = reinterpret_cast<uint8_t *>(&whitePixel);

    TexMetadata metadata{};
    metadata.width = 1;
    metadata.height = 1;
    metadata.depth = 1;
    metadata.arraySize = 1;
    metadata.mipLevels = 1;
    metadata.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    metadata.dimension = TEX_DIMENSION_TEXTURE2D;

    whiteTextureId_ = CreateTexture(&image, 1, metadata);

    Image cubeImages[6]{};
    for (Image &cubeImage : cubeImages) {
        cubeImage = image;
    }
    TexMetadata cubeMetadata = metadata;
    cubeMetadata.arraySize = 6;
    cubeMetadata.miscFlags = TEX_MISC_TEXTURECUBE;
    whiteCubeTextureId_ = CreateTexture(cubeImages, _countof(cubeImages),
                                        cubeMetadata);

    uint32_t blackPixel = 0xFF000000;
    image.pixels = reinterpret_cast<uint8_t *>(&blackPixel);
    Image blackCubeImages[6]{};
    for (Image &cubeImage : blackCubeImages) {
        cubeImage = image;
    }
    blackCubeTextureId_ = CreateTexture(blackCubeImages, _countof(blackCubeImages),
                                        cubeMetadata);

    uint32_t flatNormalPixel = 0xFFFF8080;
    image.pixels = reinterpret_cast<uint8_t *>(&flatNormalPixel);
    defaultNormalTextureId_ = CreateTexture(&image, 1, metadata);
    const auto hasExpectedDefaultResource =
        [this](uint32_t textureId, UINT16 arraySize, bool isCube) {
            if (!IsValidTextureId(textureId)) {
                return false;
            }
            const Texture &texture = textures_[textureId].texture;
            if (texture.arraySize != arraySize || texture.isCube != isCube) {
                return false;
            }
            ID3D12Resource *resource = textures_[textureId].texture.resource.Get();
            if (resource == nullptr) {
                return false;
            }
            const D3D12_RESOURCE_DESC desc = resource->GetDesc();
            return desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
                   desc.Width == 1 && desc.Height == 1 &&
                   desc.DepthOrArraySize == arraySize && desc.MipLevels == 1 &&
                   desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM;
        };

    if (!hasExpectedDefaultResource(whiteTextureId_, 1, false) ||
        !hasExpectedDefaultResource(whiteCubeTextureId_, 6, true) ||
        !hasExpectedDefaultResource(blackCubeTextureId_, 6, true) ||
        !hasExpectedDefaultResource(defaultNormalTextureId_, 1, false) ||
        whiteCubeTextureId_ == whiteTextureId_ ||
        blackCubeTextureId_ == whiteTextureId_ ||
        defaultNormalTextureId_ == whiteTextureId_ ||
        blackCubeTextureId_ == whiteCubeTextureId_) {
        return;
    }
    initializeGuard.Commit();
}

bool TextureManager::Finalize() { return Finalize(false); }

bool TextureManager::Finalize(bool allowFrameAbort) {
    bool hasFrameUploadBuffers = false;
    for (const auto &buffers : frameUploadBuffers_) {
        if (!buffers.empty()) {
            hasFrameUploadBuffers = true;
            break;
        }
    }
    const bool hasGpuResources =
        !textures_.empty() || !uploadBuffers_.empty() || hasFrameUploadBuffers;
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources, allowFrameAbort)) {
        return false;
    }

    StopAsyncLoads();
    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }
    ReleaseUploadBuffers();

    if (srvManager_ != nullptr) {
        for (const Entry &entry : textures_) {
            srvManager_->FreeIfAllocated(entry.srvIndex);
        }
    }

    textures_.clear();
    uploadBuffers_.clear();
    frameUploadBuffers_.clear();
    filePathToTextureId_.clear();
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    if (gActiveTextureManager == this) {
        SetActiveInstance(nullptr);
    }
    whiteTextureId_ = 0;
    whiteCubeTextureId_ = 0;
    blackCubeTextureId_ = 0;
    defaultNormalTextureId_ = 0;
    lastDynamicUploadFrameIndex_ = UINT_MAX;
    return true;
}

uint32_t TextureManager::Load(const std::wstring &filePath) {
    const std::filesystem::path resolvedPath =
        PathUtils::ResolveAssetPath(filePath);
    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec)) {
        return IsValidTextureId(whiteTextureId_) ? whiteTextureId_ : UINT32_MAX;
    }

    const std::wstring pathKey = PathUtils::NormalizePathKey(resolvedPath);

    auto it = filePathToTextureId_.find(pathKey);
    if (it != filePathToTextureId_.end()) {
        if (IsValidTextureId(it->second) && it->second != whiteTextureId_) {
            return it->second;
        }
        filePathToTextureId_.erase(it);
    }

    ScratchImage scratch;
    TexMetadata metadata{};

    if (!DecodeTextureFileForLoad(resolvedPath, scratch, metadata)) {
        return IsValidTextureId(whiteTextureId_) ? whiteTextureId_
                                                 : UINT32_MAX;
    }

    uint32_t id =
        CreateTexture(scratch.GetImages(), scratch.GetImageCount(), metadata);
    if (IsValidTextureId(id) && id != whiteTextureId_) {
        try {
            filePathToTextureId_[pathKey] = id;
        } catch (...) {
        }
    }

    return id;
}

std::vector<uint32_t>
TextureManager::LoadBatch(const std::vector<std::wstring> &filePaths) {
    std::vector<uint32_t> textureIds;
    try {
        textureIds.reserve(filePaths.size());
    } catch (...) {
        return textureIds;
    }
    for (const std::wstring &filePath : filePaths) {
        textureIds.push_back(Load(filePath));
    }
    return textureIds;
}

uint32_t TextureManager::LoadFromMemory(const uint8_t *data, size_t size) {
    if (!data || size == 0) {
        return IsValidTextureId(whiteTextureId_) ? whiteTextureId_ : UINT32_MAX;
    }

    ScratchImage scratch;
    TexMetadata metadata{};

    if (!DecodeTextureMemoryForLoad(data, size, scratch, metadata)) {
        return IsValidTextureId(whiteTextureId_) ? whiteTextureId_ : UINT32_MAX;
    }

    uint32_t id =
        CreateTexture(scratch.GetImages(), scratch.GetImageCount(), metadata);

    return id;
}

uint32_t TextureManager::CreateTexture(const Image *images, size_t imageCount,
                                       const TexMetadata &metadata) {
    const uint32_t fallbackTextureId =
        IsValidTextureId(whiteTextureId_) ? whiteTextureId_ : UINT32_MAX;
    if (!dxCommon_ || !dxCommon_->GetDevice() || !srvManager_) {
        return fallbackTextureId;
    }
    if (!images || imageCount == 0 || metadata.width == 0 ||
        metadata.height == 0 || metadata.arraySize == 0 ||
        metadata.mipLevels == 0) {
        return fallbackTextureId;
    }
    if (!TextureLimits::IsMetadataWithinBudget(metadata, imageCount) ||
        !TextureLimits::AreImagesWithinDecodedBudget(images, imageCount)) {
        return fallbackTextureId;
    }
    if (metadata.dimension != TEX_DIMENSION_TEXTURE2D || metadata.depth != 1) {
        return fallbackTextureId;
    }
    const bool isCubeTexture = metadata.IsCubemap();
    if (isCubeTexture && metadata.arraySize != 6) {
        return fallbackTextureId;
    }
    if (metadata.arraySize >
        (std::numeric_limits<size_t>::max)() / metadata.mipLevels) {
        return fallbackTextureId;
    }
    const size_t expectedImageCount =
        static_cast<size_t>(metadata.arraySize) *
        static_cast<size_t>(metadata.mipLevels);
    if (imageCount != expectedImageCount) {
        return fallbackTextureId;
    }
    if (metadata.height > (std::numeric_limits<UINT>::max)() ||
        metadata.arraySize > (std::numeric_limits<UINT16>::max)() ||
        metadata.mipLevels > (std::numeric_limits<UINT16>::max)() ||
        metadata.width > (std::numeric_limits<uint32_t>::max)()) {
        return fallbackTextureId;
    }
    if (imageCount > (std::numeric_limits<UINT>::max)()) {
        return fallbackTextureId;
    }
    if (!srvManager_->CanAllocate()) {
        return fallbackTextureId;
    }
    if (textures_.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return fallbackTextureId;
    }
    try {
        textures_.reserve(textures_.size() + 1);
    } catch (...) {
        return fallbackTextureId;
    }
    for (size_t imageIndex = 0; imageIndex < imageCount; ++imageIndex) {
        if (!images[imageIndex].pixels || images[imageIndex].rowPitch == 0 ||
            images[imageIndex].slicePitch == 0) {
            return fallbackTextureId;
        }
        if (images[imageIndex].rowPitch >
                static_cast<size_t>((std::numeric_limits<LONG_PTR>::max)()) ||
            images[imageIndex].slicePitch >
                static_cast<size_t>((std::numeric_limits<LONG_PTR>::max)())) {
            return fallbackTextureId;
        }
    }

    const bool ownsUploadPass =
        dxCommon_ != nullptr && !dxCommon_->IsCommandListRecording();
    if (ownsUploadPass && !dxCommon_->BeginUpload()) {
        return fallbackTextureId;
    }
    UploadPassScope uploadPass(dxCommon_, this, ownsUploadPass);
    if (!dxCommon_->IsCommandListRecording()) {
        return fallbackTextureId;
    }

    Texture texture;

    auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        metadata.format, static_cast<UINT64>(metadata.width),
        static_cast<UINT>(metadata.height),
        static_cast<UINT16>(metadata.arraySize),
        static_cast<UINT16>(metadata.mipLevels));

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);

    if (!GpuResourceHelpers::CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &defaultHeap, D3D12_HEAP_FLAG_NONE,
            &texDesc, D3D12_RESOURCE_STATE_COPY_DEST,
            texture.resource.GetAddressOf())) {
        return fallbackTextureId;
    }

    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    try {
        subresources.resize(imageCount);
    } catch (...) {
        return fallbackTextureId;
    }
    for (size_t imageIndex = 0; imageIndex < imageCount; ++imageIndex) {
        subresources[imageIndex].pData = images[imageIndex].pixels;
        subresources[imageIndex].RowPitch = images[imageIndex].rowPitch;
        subresources[imageIndex].SlicePitch = images[imageIndex].slicePitch;
    }

    UINT64 uploadSize = GetRequiredIntermediateSize(
        texture.resource.Get(), 0, static_cast<UINT>(subresources.size()));
    if (uploadSize == 0) {
        return fallbackTextureId;
    }

    ComPtr<ID3D12Resource> uploadBuffer;

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

    if (!GpuResourceHelpers::CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &uploadHeap, D3D12_HEAP_FLAG_NONE,
            &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
            uploadBuffer.GetAddressOf())) {
        return fallbackTextureId;
    }

    try {
        if (ownsUploadPass) {
            uploadBuffers_.push_back(uploadBuffer);
        } else {
            const UINT frameIndex = dxCommon_->GetBackBufferIndex();
            if (frameIndex < frameUploadBuffers_.size()) {
                if (lastDynamicUploadFrameIndex_ != frameIndex) {
                    frameUploadBuffers_[frameIndex].clear();
                    lastDynamicUploadFrameIndex_ = frameIndex;
                }
                frameUploadBuffers_[frameIndex].push_back(uploadBuffer);
            } else {
                uploadBuffers_.push_back(uploadBuffer);
            }
        }
    } catch (...) {
        return fallbackTextureId;
    }

    ScopedSrvAllocation srvAllocation(srvManager_);
    uint32_t srvIndex = srvAllocation.Allocate();
    if (srvIndex == UINT32_MAX) {
        return fallbackTextureId;
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
        srvManager_->GetCpuHandle(srvIndex);
    if (srvHandle.ptr == 0) {
        return fallbackTextureId;
    }

    ID3D12GraphicsCommandList *cmdList = dxCommon_->GetCommandList();
    if (cmdList == nullptr) {
        return fallbackTextureId;
    }

    texture.width = static_cast<uint32_t>(metadata.width);
    texture.height = static_cast<uint32_t>(metadata.height);
    texture.arraySize = static_cast<uint16_t>(metadata.arraySize);
    texture.isCube = isCubeTexture;
    texture.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    try {
        textures_.push_back({std::move(texture), srvIndex});
    } catch (...) {
        return fallbackTextureId;
    }
    srvAllocation.Commit();
    const uint32_t textureId = static_cast<uint32_t>(textures_.size() - 1);
    if (!dxCommon_->ReserveFrameRollbacks(1)) {
        textures_[textureId] = {};
        if (srvManager_ != nullptr) {
            srvManager_->FreeIfAllocated(srvIndex);
        }
        return fallbackTextureId;
    }

    std::function<void()> rollbackTexture = [this, textureId, srvIndex]() {
        if (textureId < textures_.size()) {
            textures_[textureId] = {};
        }
        if (srvManager_ != nullptr) {
            srvManager_->FreeIfAllocated(srvIndex);
        }
    };
    std::function<void()> frameRollback = rollbackTexture;
    if (!dxCommon_->RegisterFrameRollback(this, std::move(frameRollback))) {
        rollbackTexture();
        return fallbackTextureId;
    }

    Texture &storedTexture = textures_[textureId].texture;

    const UINT64 copiedBytes =
        UpdateSubresources(cmdList, storedTexture.resource.Get(),
                           uploadBuffer.Get(), 0, 0,
                           static_cast<UINT>(subresources.size()),
                           subresources.data());
    if (copiedBytes == 0) {
        rollbackTexture();
        return fallbackTextureId;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        storedTexture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    cmdList->ResourceBarrier(1, &barrier);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    srvDesc.Format = metadata.format;
    if (isCubeTexture) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = static_cast<UINT>(metadata.mipLevels);
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    } else if (metadata.dimension == TEX_DIMENSION_TEXTURE2D &&
               metadata.arraySize > 1) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.MostDetailedMip = 0;
        srvDesc.Texture2DArray.MipLevels =
            static_cast<UINT>(metadata.mipLevels);
        srvDesc.Texture2DArray.FirstArraySlice = 0;
        srvDesc.Texture2DArray.ArraySize =
            static_cast<UINT>(metadata.arraySize);
        srvDesc.Texture2DArray.PlaneSlice = 0;
        srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;
    } else {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = static_cast<UINT>(metadata.mipLevels);
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    }

    dxCommon_->GetDevice()->CreateShaderResourceView(storedTexture.resource.Get(),
                                                     &srvDesc, srvHandle);

    if (!uploadPass.Finish()) {
        return fallbackTextureId;
    }
    return textureId;
}

void TextureManager::ReleaseUploadBuffers() {
    if (dxCommon_ && dxCommon_->IsCommandListRecording()) {
        return;
    }

    bool hasFrameUploadBuffers = false;
    for (const auto &buffers : frameUploadBuffers_) {
        if (!buffers.empty()) {
            hasFrameUploadBuffers = true;
            break;
        }
    }

    if (dxCommon_ && !dxCommon_->IsDeviceRemoved() &&
        (!uploadBuffers_.empty() || hasFrameUploadBuffers)) {
        if (!dxCommon_->WaitForGpuIfPossible()) {
            return;
        }
    }

    uploadBuffers_.clear();
    for (auto &buffers : frameUploadBuffers_) {
        buffers.clear();
    }
    lastDynamicUploadFrameIndex_ = UINT_MAX;
}

D3D12_GPU_DESCRIPTOR_HANDLE
TextureManager::GetGpuHandle(uint32_t textureId) const {
    if (!IsValidTextureId(textureId) || srvManager_ == nullptr ||
        !srvManager_->IsAllocated(textures_[textureId].srvIndex)) {
        if (srvManager_ != nullptr && IsValidTextureId(whiteTextureId_) &&
            srvManager_->IsAllocated(textures_[whiteTextureId_].srvIndex)) {
            return srvManager_->GetGpuHandle(textures_[whiteTextureId_].srvIndex);
        }
        return {};
    }
    return srvManager_->GetGpuHandle(textures_[textureId].srvIndex);
}

bool TextureManager::IsValidTextureId(uint32_t textureId) const {
    return textureId < textures_.size() &&
           textures_[textureId].texture.resource != nullptr;
}

bool TextureManager::IsCubeTextureId(uint32_t textureId) const {
    return IsValidTextureId(textureId) &&
           textures_[textureId].texture.isCube;
}

ID3D12Resource *TextureManager::GetResource(uint32_t textureId) const {
    if (!IsValidTextureId(textureId)) {
        return nullptr;
    }
    return textures_[textureId].texture.resource.Get();
}

uint32_t TextureManager::GetWidth(uint32_t id) const {
    if (!IsValidTextureId(id)) {
        return 0;
    }
    return textures_[id].texture.width;
}

uint32_t TextureManager::GetHeight(uint32_t id) const {
    if (!IsValidTextureId(id)) {
        return 0;
    }
    return textures_[id].texture.height;
}

size_t TextureManager::GetTextureCount() const {
    size_t count = 0;
    for (const Entry &entry : textures_) {
        if (entry.texture.resource) {
            ++count;
        }
    }
    return count;
}

uint64_t TextureManager::GetTextureGpuBytes() const {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return 0;
    }

    uint64_t bytes = 0;
    for (const Entry &entry : textures_) {
        ID3D12Resource *resource = entry.texture.resource.Get();
        if (resource == nullptr) {
            continue;
        }
        const D3D12_RESOURCE_DESC desc = resource->GetDesc();
        const D3D12_RESOURCE_ALLOCATION_INFO info =
            dxCommon_->GetDevice()->GetResourceAllocationInfo(0, 1, &desc);
        bytes += info.SizeInBytes;
    }
    return bytes;
}

uint64_t TextureManager::GetUploadBytes() const {
    uint64_t bytes = 0;
    for (const auto &buffer : uploadBuffers_) {
        bytes += BufferByteWidth(buffer.Get());
    }
    for (const auto &buffers : frameUploadBuffers_) {
        for (const auto &buffer : buffers) {
            bytes += BufferByteWidth(buffer.Get());
        }
    }
    return bytes;
}

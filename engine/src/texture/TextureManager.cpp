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

    void Finish() {
        if (!active_) {
            return;
        }
        dxCommon_->EndUpload();
        if (textureManager_ != nullptr) {
            textureManager_->ReleaseUploadBuffers();
        }
        active_ = false;
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

static TextureManager::DecodedTexture DecodeTextureFileForAsync(
    const std::wstring &filePath) {
    const std::filesystem::path resolvedPath = ResolveTexturePath(filePath);
    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec)) {
        return {};
    }

    TextureManager::DecodedTexture decoded{};
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

namespace {
TextureManager *gActiveTextureManager = nullptr;

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
    Finalize();
}

void TextureManager::Initialize(DirectXCommon *dxCommon,
                                SrvManager *srvManager) {
    if (!dxCommon || !srvManager) {
        Finalize();
        return;
    }
    Finalize();

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    SetActiveInstance(this);
    TextureManagerInitializationGuard initializeGuard(*this);

    textures_.clear();
    uploadBuffers_.clear();
    frameUploadBuffers_.clear();
    frameUploadBuffers_.resize(dxCommon_->GetSwapChainBufferCount());
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
    initializeGuard.Commit();
}

void TextureManager::Finalize() {
    asyncRequests_.clear();
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
}

uint32_t TextureManager::Load(const std::wstring &filePath) {
    const std::filesystem::path resolvedPath = ResolveTexturePath(filePath);
    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec)) {
        return IsValidTextureId(whiteTextureId_) ? whiteTextureId_ : UINT32_MAX;
    }

    const std::wstring pathKey = NormalizePathKey(resolvedPath);

    auto it = filePathToTextureId_.find(pathKey);
    if (it != filePathToTextureId_.end()) {
        if (IsValidTextureId(it->second) && it->second != whiteTextureId_) {
            return it->second;
        }
        filePathToTextureId_.erase(it);
    }

    ScratchImage scratch;
    TexMetadata metadata{};

    const std::wstring ext = resolvedPath.extension().wstring();

    if (_wcsicmp(ext.c_str(), L".dds") == 0) {
        if (FAILED(LoadFromDDSFile(resolvedPath.c_str(), DDS_FLAGS_NONE,
                                   &metadata, scratch))) {
            return IsValidTextureId(whiteTextureId_) ? whiteTextureId_
                                                     : UINT32_MAX;
        }
    } else {
        if (FAILED(LoadFromWICFile(resolvedPath.c_str(),
                                   WIC_FLAGS_IGNORE_SRGB, &metadata,
                                   scratch))) {
            return IsValidTextureId(whiteTextureId_) ? whiteTextureId_
                                                     : UINT32_MAX;
        }
    }

    uint32_t id =
        CreateTexture(scratch.GetImages(), scratch.GetImageCount(), metadata);
    if (IsValidTextureId(id) && id != whiteTextureId_) {
        filePathToTextureId_[pathKey] = id;
    }

    return id;
}

std::vector<uint32_t>
TextureManager::LoadBatch(const std::vector<std::wstring> &filePaths) {
    std::vector<uint32_t> textureIds;
    textureIds.reserve(filePaths.size());
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

    if (FAILED(LoadFromWICMemory(data, size, WIC_FLAGS_IGNORE_SRGB, &metadata,
                                 scratch))) {
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
    if (metadata.dimension != TEX_DIMENSION_TEXTURE2D || metadata.depth != 1) {
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
    if (ownsUploadPass) {
        dxCommon_->BeginUpload();
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

    const HRESULT textureResult =
        dxCommon_->GetDevice()->CreateCommittedResource(
            &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&texture.resource));
    if (FAILED(textureResult) || !texture.resource) {
        return fallbackTextureId;
    }

    std::vector<D3D12_SUBRESOURCE_DATA> subresources(imageCount);
    for (size_t imageIndex = 0; imageIndex < imageCount; ++imageIndex) {
        subresources[imageIndex].pData = images[imageIndex].pixels;
        subresources[imageIndex].RowPitch = images[imageIndex].rowPitch;
        subresources[imageIndex].SlicePitch = images[imageIndex].slicePitch;
    }

    UINT64 uploadSize = GetRequiredIntermediateSize(
        texture.resource.Get(), 0, static_cast<UINT>(subresources.size()));

    ComPtr<ID3D12Resource> uploadBuffer;

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

    const HRESULT uploadResult =
        dxCommon_->GetDevice()->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&uploadBuffer));
    if (FAILED(uploadResult) || !uploadBuffer) {
        return fallbackTextureId;
    }

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

    UpdateSubresources(cmdList, texture.resource.Get(), uploadBuffer.Get(), 0,
                       0, static_cast<UINT>(subresources.size()),
                       subresources.data());

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    cmdList->ResourceBarrier(1, &barrier);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    srvDesc.Format = metadata.format;
    if (metadata.IsCubemap()) {
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

    dxCommon_->GetDevice()->CreateShaderResourceView(texture.resource.Get(),
                                                     &srvDesc, srvHandle);

    texture.width = static_cast<uint32_t>(metadata.width);
    texture.height = static_cast<uint32_t>(metadata.height);
    texture.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    textures_.push_back({std::move(texture), srvIndex});
    srvAllocation.Commit();

    uint32_t textureId = static_cast<uint32_t>(textures_.size() - 1);

    uploadPass.Finish();

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
        dxCommon_->WaitForGpuIfPossible();
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

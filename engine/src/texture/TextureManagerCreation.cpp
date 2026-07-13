#include "../graphics/internal/GpuResourceScopes.h"
#include "internal/TextureManagerInternal.h"
#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/SrvManager.h"
#include "texture/Texture.h"
#include "texture/TextureLimits.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <utility>
#include <vector>
#include <wrl.h>

using namespace DirectX;
using GraphicsResourceScopes::ScopedSrvAllocations;
using GraphicsResourceScopes::ScopedUploadPass;
using Microsoft::WRL::ComPtr;

namespace {

bool IsTextureCreationRequestValid(const Image *images, size_t imageCount,
                                   const TexMetadata &metadata) {
    if (!images || imageCount == 0 || metadata.width == 0 ||
        metadata.height == 0 || metadata.arraySize == 0 ||
        metadata.mipLevels == 0) {
        return false;
    }
    if (!TextureLimits::IsMetadataWithinBudget(metadata, imageCount) ||
        !TextureLimits::AreImagesWithinDecodedBudget(images, imageCount)) {
        return false;
    }
    if (metadata.dimension != TEX_DIMENSION_TEXTURE2D || metadata.depth != 1) {
        return false;
    }
    if (metadata.IsCubemap() && metadata.arraySize != 6) {
        return false;
    }
    if (metadata.arraySize >
        (std::numeric_limits<size_t>::max)() / metadata.mipLevels) {
        return false;
    }
    const size_t expectedImageCount =
        static_cast<size_t>(metadata.arraySize) *
        static_cast<size_t>(metadata.mipLevels);
    return imageCount == expectedImageCount;
}

bool CanRepresentTextureMetadataForD3D12(const TexMetadata &metadata,
                                         size_t imageCount) {
    return metadata.height <= (std::numeric_limits<UINT>::max)() &&
           metadata.arraySize <= (std::numeric_limits<UINT16>::max)() &&
           metadata.mipLevels <= (std::numeric_limits<UINT16>::max)() &&
           metadata.width <= (std::numeric_limits<uint32_t>::max)() &&
           imageCount <= (std::numeric_limits<UINT>::max)();
}

bool AreTextureImagesUploadable(const Image *images, size_t imageCount) {
    for (size_t imageIndex = 0; imageIndex < imageCount; ++imageIndex) {
        if (!images[imageIndex].pixels || images[imageIndex].rowPitch == 0 ||
            images[imageIndex].slicePitch == 0) {
            return false;
        }
        if (images[imageIndex].rowPitch >
                static_cast<size_t>((std::numeric_limits<LONG_PTR>::max)()) ||
            images[imageIndex].slicePitch >
                static_cast<size_t>((std::numeric_limits<LONG_PTR>::max)())) {
            return false;
        }
    }
    return true;
}

bool TryBuildTextureSubresources(
    const Image *images, size_t imageCount,
    std::vector<D3D12_SUBRESOURCE_DATA> &subresources) {
    try {
        subresources.resize(imageCount);
    } catch (...) {
        return false;
    }
    for (size_t imageIndex = 0; imageIndex < imageCount; ++imageIndex) {
        subresources[imageIndex].pData = images[imageIndex].pixels;
        subresources[imageIndex].RowPitch = images[imageIndex].rowPitch;
        subresources[imageIndex].SlicePitch = images[imageIndex].slicePitch;
    }
    return true;
}

D3D12_SHADER_RESOURCE_VIEW_DESC BuildTextureSrvDesc(
    const TexMetadata &metadata, bool isCubeTexture) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = metadata.format;
    if (isCubeTexture) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = static_cast<UINT>(metadata.mipLevels);
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
        return srvDesc;
    }
    if (metadata.dimension == TEX_DIMENSION_TEXTURE2D &&
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
        return srvDesc;
    }

    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = static_cast<UINT>(metadata.mipLevels);
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    return srvDesc;
}

Texture BuildTextureMetadata(const TexMetadata &metadata, bool isCubeTexture) {
    Texture texture{};
    texture.width = static_cast<uint32_t>(metadata.width);
    texture.height = static_cast<uint32_t>(metadata.height);
    texture.arraySize = static_cast<uint16_t>(metadata.arraySize);
    texture.isCube = isCubeTexture;
    texture.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    return texture;
}

bool CreateTextureResource(ID3D12Device *device, const TexMetadata &metadata,
                           Texture &texture) {
    auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        metadata.format, static_cast<UINT64>(metadata.width),
        static_cast<UINT>(metadata.height),
        static_cast<UINT16>(metadata.arraySize),
        static_cast<UINT16>(metadata.mipLevels));

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    return GpuResourceHelpers::CreateCommittedResourceChecked(
        device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, texture.resource.GetAddressOf());
}

bool CreateTextureUploadBuffer(ID3D12Device *device, UINT64 uploadSize,
                               ComPtr<ID3D12Resource> &uploadBuffer) {
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    return GpuResourceHelpers::CreateCommittedResourceChecked(
        device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, uploadBuffer.GetAddressOf());
}

bool CopyTextureSubresources(ID3D12GraphicsCommandList *cmdList,
                             Texture &storedTexture,
                             ID3D12Resource *uploadBuffer,
                             std::vector<D3D12_SUBRESOURCE_DATA> &subresources) {
    const UINT64 copiedBytes =
        UpdateSubresources(cmdList, storedTexture.resource.Get(), uploadBuffer,
                           0, 0, static_cast<UINT>(subresources.size()),
                           subresources.data());
    if (copiedBytes == 0) {
        return false;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        storedTexture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);
    return true;
}

} // namespace

bool TextureManager::ReserveTextureStorage() {
    if (state_->textures.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return false;
    }
    try {
        state_->textures.reserve(state_->textures.size() + 1);
    } catch (...) {
        return false;
    }
    return true;
}

bool TextureManager::StoreTextureUploadBuffer(
    const ComPtr<ID3D12Resource> &uploadBuffer, bool ownsUploadPass) {
    try {
        if (ownsUploadPass) {
            state_->uploadBuffers.push_back(uploadBuffer);
            return true;
        }

        const UINT frameIndex = dxCommon_->GetBackBufferIndex();
        if (frameIndex < state_->frameUploadBuffers.size()) {
            if (state_->lastDynamicUploadFrameIndex != frameIndex) {
                state_->frameUploadBuffers[frameIndex].clear();
                state_->lastDynamicUploadFrameIndex = frameIndex;
            }
            state_->frameUploadBuffers[frameIndex].push_back(uploadBuffer);
            return true;
        }

        state_->uploadBuffers.push_back(uploadBuffer);
    } catch (...) {
        return false;
    }
    return true;
}

bool TextureManager::StoreTextureEntry(Texture &&texture, uint32_t srvIndex,
                                       uint32_t &textureId) {
    try {
        state_->textures.push_back({std::move(texture), srvIndex});
    } catch (...) {
        return false;
    }
    textureId = static_cast<uint32_t>(state_->textures.size() - 1);
    return true;
}

void TextureManager::RollBackTextureEntry(uint32_t textureId,
                                          uint32_t srvIndex) {
    if (textureId < state_->textures.size()) {
        state_->textures[textureId] = {};
    }
    if (srvManager_ != nullptr) {
        srvManager_->FreeIfAllocated(srvIndex);
    }
}

bool TextureManager::RegisterTextureFrameRollback(uint32_t textureId,
                                                  uint32_t srvIndex) {
    if (!dxCommon_->ReserveFrameRollbacks(1)) {
        return false;
    }
    auto rollbackArmed = std::make_shared<bool>(true);
    std::function<void()> rollbackTexture = [this, textureId, srvIndex,
                                             rollbackArmed]() {
        if (!*rollbackArmed) {
            return;
        }
        *rollbackArmed = false;
        RollBackTextureEntry(textureId, srvIndex);
    };
    return dxCommon_->RegisterFrameRollback(this, std::move(rollbackTexture));
}

uint32_t TextureManager::CreateTexture(const Image* images, size_t imageCount,
                                       const TexMetadata& metadata) {
    const uint32_t fallbackTextureId = GetWhiteFallbackTextureId();
    if (!dxCommon_ || !dxCommon_->GetDevice() || !srvManager_) {
        return fallbackTextureId;
    }
    if (!IsTextureCreationRequestValid(images, imageCount, metadata) ||
        !CanRepresentTextureMetadataForD3D12(metadata, imageCount)) {
        return fallbackTextureId;
    }
    const bool isCubeTexture = metadata.IsCubemap();
    if (!srvManager_->CanAllocate()) {
        return fallbackTextureId;
    }
    if (!ReserveTextureStorage()) {
        return fallbackTextureId;
    }
    if (!AreTextureImagesUploadable(images, imageCount)) {
        return fallbackTextureId;
    }

    const bool ownsUploadPass = dxCommon_ != nullptr && !dxCommon_->IsCommandListRecording();
    if (ownsUploadPass && !dxCommon_->BeginUpload()) {
        return fallbackTextureId;
    }
    ScopedUploadPass uploadPass(dxCommon_, this, ownsUploadPass);
    if (!dxCommon_->IsCommandListRecording()) {
        return fallbackTextureId;
    }

    Texture texture = BuildTextureMetadata(metadata, isCubeTexture);
    if (!CreateTextureResource(dxCommon_->GetDevice(), metadata, texture)) {
        return fallbackTextureId;
    }

    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    if (!TryBuildTextureSubresources(images, imageCount, subresources)) {
        return fallbackTextureId;
    }

    UINT64 uploadSize = GetRequiredIntermediateSize(texture.resource.Get(), 0,
                                                    static_cast<UINT>(subresources.size()));
    if (uploadSize == 0) {
        return fallbackTextureId;
    }

    ComPtr<ID3D12Resource> uploadBuffer;
    if (!CreateTextureUploadBuffer(dxCommon_->GetDevice(), uploadSize,
                                   uploadBuffer)) {
        return fallbackTextureId;
    }

    if (!StoreTextureUploadBuffer(uploadBuffer, ownsUploadPass)) {
        return fallbackTextureId;
    }
    ScopedSrvAllocations srvAllocations(srvManager_);
    uint32_t srvIndex = srvAllocations.Allocate();
    if (srvIndex == kInvalidResourceId) {
        return fallbackTextureId;
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = srvManager_->GetCpuHandle(srvIndex);
    if (srvHandle.ptr == 0) {
        return fallbackTextureId;
    }

    ID3D12GraphicsCommandList* cmdList = dxCommon_->GetCommandList();
    if (cmdList == nullptr) {
        return fallbackTextureId;
    }

    uint32_t textureId = kInvalidResourceId;
    if (!StoreTextureEntry(std::move(texture), srvIndex, textureId)) {
        return fallbackTextureId;
    }
    srvAllocations.Commit();
    if (!RegisterTextureFrameRollback(textureId, srvIndex)) {
        RollBackTextureEntry(textureId, srvIndex);
        return fallbackTextureId;
    }

    Texture& storedTexture = state_->textures[textureId].texture;

    if (!CopyTextureSubresources(cmdList, storedTexture, uploadBuffer.Get(),
                                 subresources)) {
        RollBackTextureEntry(textureId, srvIndex);
        return fallbackTextureId;
    }

    const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc =
        BuildTextureSrvDesc(metadata, isCubeTexture);

    dxCommon_->GetDevice()->CreateShaderResourceView(storedTexture.resource.Get(), &srvDesc,
                                                     srvHandle);

    if (!uploadPass.Finish()) {
        return fallbackTextureId;
    }
    return textureId;
}

void TextureManager::ReleaseUploadBuffers() {
    if (dxCommon_ && dxCommon_->IsCommandListRecording()) {
        return;
    }

    if (dxCommon_ && !dxCommon_->IsDeviceRemoved() &&
        !state_->uploadBuffers.empty()) {
        if (!dxCommon_->WaitForGpuIfPossible()) {
            return;
        }
    }

    state_->uploadBuffers.clear();
}

void TextureManager::ReleaseCompletedFrameResources() {
    if (dxCommon_ == nullptr || !dxCommon_->IsCommandListRecording()) {
        return;
    }
    const UINT frameIndex = dxCommon_->GetBackBufferIndex();
    if (frameIndex < state_->frameUploadBuffers.size()) {
        state_->frameUploadBuffers[frameIndex].clear();
    }
    state_->lastDynamicUploadFrameIndex = UINT_MAX;
}

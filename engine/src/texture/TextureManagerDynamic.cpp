#include "../graphics/internal/GpuResourceScopes.h"
#include "internal/TextureManagerInternal.h"
#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/SrvManager.h"
#include "texture/Texture.h"
#include "texture/TextureManager.h"

#include <array>
#include <limits>

using namespace DirectX;
using GraphicsResourceScopes::ScopedUploadPass;
using Microsoft::WRL::ComPtr;

namespace {

template <typename TextureManagerState>
void PrepareDynamicUploadFrame(TextureManagerState& state, UINT frameIndex) {
    if (frameIndex < state.frameUploadBuffers.size() &&
        state.lastDynamicUploadFrameIndex != frameIndex) {
        state.frameUploadBuffers[frameIndex].clear();
        state.lastDynamicUploadFrameIndex = frameIndex;
    }
}

template <typename TextureManagerState>
void RetainDynamicUploadBuffer(
    TextureManagerState& state, UINT frameIndex,
    const ComPtr<ID3D12Resource>& uploadBuffer) {
    if (frameIndex < state.frameUploadBuffers.size()) {
        state.frameUploadBuffers[frameIndex].push_back(uploadBuffer);
    } else {
        state.uploadBuffers.push_back(uploadBuffer);
    }
}

template <typename TextureManagerState>
void UploadTextureSubresources(TextureManager* textureManager,
                               DirectXCommon* dxCommon,
                               TextureManagerState& state,
                               uint32_t textureId,
                               Texture& texture,
                               D3D12_SUBRESOURCE_DATA* subresources,
                               UINT subresourceCount) {
    const bool ownsUploadPass = !dxCommon->IsCommandListRecording();
    if (ownsUploadPass && !dxCommon->BeginUpload()) {
        return;
    }
    ScopedUploadPass uploadPass(dxCommon, textureManager, ownsUploadPass);
    if (!dxCommon->IsCommandListRecording()) {
        return;
    }

    const UINT frameIndex = dxCommon->GetBackBufferIndex();
    PrepareDynamicUploadFrame(state, frameIndex);

    const UINT64 uploadSize =
        GetRequiredIntermediateSize(texture.resource.Get(), 0, subresourceCount);
    if (uploadSize == 0) {
        return;
    }

    ComPtr<ID3D12Resource> uploadBuffer;
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    if (!GpuResourceHelpers::CreateCommittedResourceChecked(
            dxCommon->GetDevice(), &uploadHeap, D3D12_HEAP_FLAG_NONE,
            &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
            uploadBuffer.GetAddressOf())) {
        return;
    }

    ID3D12GraphicsCommandList* cmdList = dxCommon->GetCommandList();
    if (cmdList == nullptr) {
        return;
    }

    RetainDynamicUploadBuffer(state, frameIndex, uploadBuffer);
    const D3D12_RESOURCE_STATES previousState = texture.state;
    bool rollbackRegistered = false;
    auto registerStateRollback = [&]() -> bool {
        if (rollbackRegistered || !dxCommon->IsCommandListRecording()) {
            return true;
        }
        if (!dxCommon->RegisterFrameRollback(
                textureManager, [&state, textureId, previousState]() {
                    if (textureId < state.textures.size()) {
                        state.textures[textureId].texture.state = previousState;
                    }
                })) {
            return false;
        }
        rollbackRegistered = true;
        return true;
    };
    if (texture.state != D3D12_RESOURCE_STATE_COPY_DEST) {
        if (!registerStateRollback()) {
            return;
        }
        auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
            texture.resource.Get(), texture.state,
            D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->ResourceBarrier(1, &toCopyDest);
        texture.state = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    const UINT64 copiedBytes =
        UpdateSubresources(cmdList, texture.resource.Get(),
                           uploadBuffer.Get(), 0, 0, subresourceCount,
                           subresources);
    if (copiedBytes == 0) {
        if (texture.state != previousState) {
            auto restoreState = CD3DX12_RESOURCE_BARRIER::Transition(
                texture.resource.Get(), texture.state, previousState);
            cmdList->ResourceBarrier(1, &restoreState);
            texture.state = previousState;
        }
        if (!uploadPass.Finish()) {
            texture.state = previousState;
        }
        return;
    }

    auto toShaderResource = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (!registerStateRollback()) {
        return;
    }
    cmdList->ResourceBarrier(1, &toShaderResource);
    texture.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    if (!uploadPass.Finish()) {
        texture.state = previousState;
    }
}

} // namespace

uint32_t TextureManager::CreateFromRgbaPixels(uint32_t width, uint32_t height,
                                              const uint8_t* pixels) {
    return CreateTexture2D(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, pixels,
                           static_cast<size_t>(width) * 4u);
}

uint32_t TextureManager::CreateFromRgbaPixelsSrgb(uint32_t width, uint32_t height,
                                                  const uint8_t* pixels) {
    return CreateTexture2D(width, height, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, pixels,
                           static_cast<size_t>(width) * 4u);
}

uint32_t
TextureManager::CreateCubeFromRgbaPixels(uint32_t size,
                                         const uint8_t *const *facePixels) {
    const uint32_t fallbackTextureId =
        IsValidTextureId(state_->blackCubeTextureId)
            ? state_->blackCubeTextureId
            : kInvalidResourceId;
    if (size == 0 || !facePixels) {
        return fallbackTextureId;
    }
    if (static_cast<size_t>(size) >
        (std::numeric_limits<size_t>::max)() / 4u) {
        return fallbackTextureId;
    }

    std::array<Image, 6> images{};
    const size_t rowPitch = static_cast<size_t>(size) * 4u;
    if (rowPitch > (std::numeric_limits<size_t>::max)() /
                       static_cast<size_t>(size)) {
        return fallbackTextureId;
    }
    const size_t slicePitch = rowPitch * static_cast<size_t>(size);
    for (size_t face = 0; face < images.size(); ++face) {
        if (!facePixels[face]) {
            return fallbackTextureId;
        }
        images[face].width = size;
        images[face].height = size;
        images[face].format = DXGI_FORMAT_R8G8B8A8_UNORM;
        images[face].rowPitch = rowPitch;
        images[face].slicePitch = slicePitch;
        images[face].pixels = const_cast<uint8_t *>(facePixels[face]);
    }

    TexMetadata metadata{};
    metadata.width = size;
    metadata.height = size;
    metadata.depth = 1;
    metadata.arraySize = 6;
    metadata.mipLevels = 1;
    metadata.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    metadata.dimension = TEX_DIMENSION_TEXTURE2D;
    metadata.miscFlags = TEX_MISC_TEXTURECUBE;

    const uint32_t textureId = CreateTexture(images.data(), images.size(), metadata);
    return IsCubeTextureId(textureId) ? textureId : fallbackTextureId;
}

void TextureManager::UpdateCubeFromRgbaPixels(
    uint32_t textureId, uint32_t size, const uint8_t *const *facePixels) {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    if (size == 0 || !facePixels || !IsValidTextureId(textureId) ||
        !IsCubeTextureId(textureId)) {
        return;
    }
    if (static_cast<size_t>(size) >
        (std::numeric_limits<size_t>::max)() / 4u) {
        return;
    }

    Texture &texture = state_->textures[textureId].texture;
    if (!texture.resource || texture.width <= 0 || texture.height <= 0 ||
        texture.arraySize != 6 || !texture.isCube) {
        return;
    }

    D3D12_RESOURCE_DESC textureDesc = texture.resource->GetDesc();
    if (textureDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        textureDesc.DepthOrArraySize != 6 || textureDesc.MipLevels != 1 ||
        textureDesc.Width != size || textureDesc.Height != size ||
        textureDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        return;
    }

    const size_t rowPitch = static_cast<size_t>(size) * 4u;
    if (rowPitch > (std::numeric_limits<size_t>::max)() /
                       static_cast<size_t>(size)) {
        return;
    }
    const size_t slicePitch = rowPitch * static_cast<size_t>(size);
    if (rowPitch > static_cast<size_t>((std::numeric_limits<LONG_PTR>::max)()) ||
        slicePitch >
            static_cast<size_t>((std::numeric_limits<LONG_PTR>::max)())) {
        return;
    }

    std::array<D3D12_SUBRESOURCE_DATA, 6> subresources{};
    for (size_t face = 0; face < subresources.size(); ++face) {
        if (!facePixels[face]) {
            return;
        }
        subresources[face].pData = facePixels[face];
        subresources[face].RowPitch = static_cast<LONG_PTR>(rowPitch);
        subresources[face].SlicePitch = static_cast<LONG_PTR>(slicePitch);
    }

    UploadTextureSubresources(this, dxCommon_, *state_, textureId, texture,
                              subresources.data(),
                              static_cast<UINT>(subresources.size()));
}

uint32_t TextureManager::CreateTexture2D(uint32_t width, uint32_t height, DXGI_FORMAT format,
                                         const uint8_t* pixels, size_t rowPitch) {
    const uint32_t fallbackTextureId =
        IsValidTextureId(state_->whiteTextureId) ? state_->whiteTextureId : kInvalidResourceId;
    if (width == 0 || height == 0 || !pixels || rowPitch == 0) {
        return fallbackTextureId;
    }
    if (DirectX::IsCompressed(format) || DirectX::IsDepthStencil(format)) {
        return fallbackTextureId;
    }
    const size_t bitsPerPixel = DirectX::BitsPerPixel(format);
    if (bitsPerPixel == 0) {
        return fallbackTextureId;
    }
    if (static_cast<size_t>(width) > ((std::numeric_limits<size_t>::max)() - 7u) / bitsPerPixel) {
        return fallbackTextureId;
    }
    const size_t minimumRowPitch = (static_cast<size_t>(width) * bitsPerPixel + 7u) / 8u;
    if (rowPitch < minimumRowPitch) {
        return fallbackTextureId;
    }
    if (rowPitch > (std::numeric_limits<size_t>::max)() / height) {
        return fallbackTextureId;
    }

    Image image{};
    image.width = width;
    image.height = height;
    image.format = format;
    image.rowPitch = rowPitch;
    image.slicePitch = rowPitch * height;
    image.pixels = const_cast<uint8_t*>(pixels);

    TexMetadata metadata{};
    metadata.width = width;
    metadata.height = height;
    metadata.depth = 1;
    metadata.arraySize = 1;
    metadata.mipLevels = 1;
    metadata.format = format;
    metadata.dimension = TEX_DIMENSION_TEXTURE2D;

    return CreateTexture(&image, 1, metadata);
}

void TextureManager::UpdateTexture2D(uint32_t textureId, const uint8_t* pixels, size_t rowPitch) {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    if (!pixels || rowPitch == 0 || !IsValidTextureId(textureId)) {
        return;
    }

    Texture& texture = state_->textures[textureId].texture;
    if (!texture.resource || texture.width <= 0 || texture.height <= 0) {
        return;
    }

    D3D12_RESOURCE_DESC textureDesc = texture.resource->GetDesc();
    if (textureDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        textureDesc.DepthOrArraySize != 1 || textureDesc.MipLevels != 1) {
        return;
    }
    if (textureDesc.Width > static_cast<UINT64>((std::numeric_limits<int>::max)()) ||
        textureDesc.Height > static_cast<UINT>((std::numeric_limits<int>::max)()) ||
        static_cast<int>(textureDesc.Width) != texture.width ||
        static_cast<int>(textureDesc.Height) != texture.height) {
        return;
    }
    const size_t bitsPerPixel = DirectX::BitsPerPixel(textureDesc.Format);
    if (bitsPerPixel == 0) {
        return;
    }
    if (DirectX::IsCompressed(textureDesc.Format) || DirectX::IsDepthStencil(textureDesc.Format)) {
        return;
    }
    const size_t width = static_cast<size_t>(texture.width);
    if (width > ((std::numeric_limits<size_t>::max)() - 7u) / bitsPerPixel) {
        return;
    }
    const size_t expectedRowPitch = (width * bitsPerPixel + 7u) / 8u;
    if (rowPitch < expectedRowPitch) {
        return;
    }

    D3D12_SUBRESOURCE_DATA subresource{};
    subresource.pData = pixels;
    if (rowPitch > static_cast<size_t>((std::numeric_limits<LONG_PTR>::max)())) {
        return;
    }
    if (rowPitch > (std::numeric_limits<size_t>::max)() / static_cast<size_t>(texture.height)) {
        return;
    }
    const size_t slicePitch = rowPitch * static_cast<size_t>(texture.height);
    if (slicePitch > static_cast<size_t>((std::numeric_limits<LONG_PTR>::max)())) {
        return;
    }
    subresource.RowPitch = static_cast<LONG_PTR>(rowPitch);
    subresource.SlicePitch = static_cast<LONG_PTR>(slicePitch);

    UploadTextureSubresources(this, dxCommon_, *state_, textureId, texture,
                              &subresource, 1);
}

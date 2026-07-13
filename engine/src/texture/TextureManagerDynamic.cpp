#include "../graphics/internal/GpuResourceScopes.h"
#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/SrvManager.h"
#include "internal/TextureManagerInternal.h"
#include "texture/Texture.h"
#include "texture/TextureManager.h"

#include <array>
#include <exception>
#include <limits>
#include <new>
#include <vector>

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
bool RetainDynamicUploadBuffer(TextureManagerState& state, UINT frameIndex,
                               const ComPtr<ID3D12Resource>& uploadBuffer) {
    try {
        if (frameIndex < state.frameUploadBuffers.size()) {
            state.frameUploadBuffers[frameIndex].push_back(uploadBuffer);
        } else {
            state.uploadBuffers.push_back(uploadBuffer);
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool CreateDynamicTextureUploadBuffer(const DirectXCommon* dxCommon, UINT64 uploadSize,
                                      ComPtr<ID3D12Resource>& uploadBuffer) {
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    return GpuResourceHelpers::CreateCommittedResourceChecked(
        dxCommon->GetDevice(), &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, uploadBuffer.GetAddressOf());
}

template <typename TextureManagerState>
bool RegisterTextureStateRollback(TextureManager* textureManager, DirectXCommon* dxCommon,
                                  TextureManagerState& state, uint32_t textureId,
                                  D3D12_RESOURCE_STATES previousState, bool& rollbackRegistered) {
    if (rollbackRegistered || !dxCommon->IsCommandListRecording()) {
        return true;
    }
    if (!dxCommon->RegisterFrameRollback(textureManager, [&state, textureId, previousState]() {
            if (textureId < state.textures.size()) {
                state.textures[textureId].texture.state = previousState;
            }
        })) {
        return false;
    }
    rollbackRegistered = true;
    return true;
}

bool TransitionTextureForUpload(ID3D12GraphicsCommandList* commandList, Texture& texture,
                                D3D12_RESOURCE_STATES previousState) {
    if (texture.state == D3D12_RESOURCE_STATE_COPY_DEST) {
        return true;
    }
    auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(), texture.state,
                                                           D3D12_RESOURCE_STATE_COPY_DEST);
    commandList->ResourceBarrier(1, &toCopyDest);
    texture.state = D3D12_RESOURCE_STATE_COPY_DEST;
    (void)previousState;
    return true;
}

void RestoreTextureStateAfterFailedCopy(ID3D12GraphicsCommandList* commandList, Texture& texture,
                                        D3D12_RESOURCE_STATES previousState) {
    if (texture.state == previousState) {
        return;
    }
    auto restoreState =
        CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(), texture.state, previousState);
    commandList->ResourceBarrier(1, &restoreState);
    texture.state = previousState;
}

void TransitionTextureToShaderResource(ID3D12GraphicsCommandList* commandList, Texture& texture) {
    auto toShaderResource =
        CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1, &toShaderResource);
    texture.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

template <typename TextureManagerState>
void UploadTextureSubresources(TextureManager* textureManager, DirectXCommon* dxCommon,
                               TextureManagerState& state, uint32_t textureId, Texture& texture,
                               D3D12_SUBRESOURCE_DATA* subresources, UINT subresourceCount) {
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
    if (!CreateDynamicTextureUploadBuffer(dxCommon, uploadSize, uploadBuffer)) {
        return;
    }

    ID3D12GraphicsCommandList* cmdList = dxCommon->GetCommandList();
    if (cmdList == nullptr) {
        return;
    }

    if (!RetainDynamicUploadBuffer(state, frameIndex, uploadBuffer)) {
        return;
    }
    const D3D12_RESOURCE_STATES previousState = texture.state;
    bool rollbackRegistered = false;
    if (texture.state != D3D12_RESOURCE_STATE_COPY_DEST) {
        if (!RegisterTextureStateRollback(textureManager, dxCommon, state, textureId, previousState,
                                          rollbackRegistered)) {
            return;
        }
        TransitionTextureForUpload(cmdList, texture, previousState);
    }

    const UINT64 copiedBytes = UpdateSubresources(
        cmdList, texture.resource.Get(), uploadBuffer.Get(), 0, 0, subresourceCount, subresources);
    if (copiedBytes == 0) {
        RestoreTextureStateAfterFailedCopy(cmdList, texture, previousState);
        if (!uploadPass.Finish()) {
            texture.state = previousState;
        }
        return;
    }

    if (!RegisterTextureStateRollback(textureManager, dxCommon, state, textureId, previousState,
                                      rollbackRegistered)) {
        return;
    }
    TransitionTextureToShaderResource(cmdList, texture);

    if (!uploadPass.Finish()) {
        texture.state = previousState;
    }
}

bool TryResolveRgbaTexturePitches(uint32_t width, uint32_t height, size_t& rowPitch,
                                  size_t& slicePitch) {
    if (static_cast<size_t>(width) > (std::numeric_limits<size_t>::max)() / 4u) {
        return false;
    }
    rowPitch = static_cast<size_t>(width) * 4u;
    if (rowPitch > (std::numeric_limits<size_t>::max)() / static_cast<size_t>(height)) {
        return false;
    }
    slicePitch = rowPitch * static_cast<size_t>(height);
    return rowPitch <= static_cast<size_t>((std::numeric_limits<LONG_PTR>::max)()) &&
           slicePitch <= static_cast<size_t>((std::numeric_limits<LONG_PTR>::max)());
}

bool IsCubeTextureUpdateTargetValid(const Texture& texture, uint32_t size) {
    if (!texture.resource || texture.width <= 0 || texture.height <= 0 || texture.arraySize != 6 ||
        !texture.isCube) {
        return false;
    }

    D3D12_RESOURCE_DESC textureDesc = texture.resource->GetDesc();
    return textureDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
           textureDesc.DepthOrArraySize == 6 && textureDesc.MipLevels == 1 &&
           textureDesc.Width == size && textureDesc.Height == size &&
           textureDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM;
}

bool TryBuildCubeSubresources(const uint8_t* const* facePixels, size_t rowPitch, size_t slicePitch,
                              std::array<D3D12_SUBRESOURCE_DATA, 6>& subresources) {
    for (size_t face = 0; face < subresources.size(); ++face) {
        if (!facePixels[face]) {
            return false;
        }
        subresources[face].pData = facePixels[face];
        subresources[face].RowPitch = static_cast<LONG_PTR>(rowPitch);
        subresources[face].SlicePitch = static_cast<LONG_PTR>(slicePitch);
    }
    return true;
}

bool IsTexture2DUpdateTargetValid(const Texture& texture, const D3D12_RESOURCE_DESC& textureDesc) {
    if (!texture.resource || texture.width <= 0 || texture.height <= 0) {
        return false;
    }
    if (textureDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        textureDesc.DepthOrArraySize != 1 || textureDesc.MipLevels != 1) {
        return false;
    }
    return textureDesc.Width <= static_cast<UINT64>((std::numeric_limits<int>::max)()) &&
           textureDesc.Height <= static_cast<UINT>((std::numeric_limits<int>::max)()) &&
           static_cast<int>(textureDesc.Width) == texture.width &&
           static_cast<int>(textureDesc.Height) == texture.height;
}

bool TryBuildTexture2DSubresource(const Texture& texture, const D3D12_RESOURCE_DESC& textureDesc,
                                  const uint8_t* pixels, size_t rowPitch,
                                  D3D12_SUBRESOURCE_DATA& subresource) {
    const size_t bitsPerPixel = DirectX::BitsPerPixel(textureDesc.Format);
    if (bitsPerPixel == 0 || DirectX::IsCompressed(textureDesc.Format) ||
        DirectX::IsDepthStencil(textureDesc.Format)) {
        return false;
    }
    const size_t width = static_cast<size_t>(texture.width);
    if (width > ((std::numeric_limits<size_t>::max)() - 7u) / bitsPerPixel) {
        return false;
    }
    const size_t expectedRowPitch = (width * bitsPerPixel + 7u) / 8u;
    if (rowPitch < expectedRowPitch ||
        rowPitch > static_cast<size_t>((std::numeric_limits<LONG_PTR>::max)()) ||
        rowPitch > (std::numeric_limits<size_t>::max)() / static_cast<size_t>(texture.height)) {
        return false;
    }
    const size_t slicePitch = rowPitch * static_cast<size_t>(texture.height);
    if (slicePitch > static_cast<size_t>((std::numeric_limits<LONG_PTR>::max)())) {
        return false;
    }
    subresource.pData = pixels;
    subresource.RowPitch = static_cast<LONG_PTR>(rowPitch);
    subresource.SlicePitch = static_cast<LONG_PTR>(slicePitch);
    return true;
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

uint32_t TextureManager::CreateCubeFromRgbaPixels(uint32_t size, const uint8_t* const* facePixels) {
    const uint32_t fallbackTextureId = IsValidTextureId(state_->blackCubeTextureId)
                                           ? state_->blackCubeTextureId
                                           : kInvalidResourceId;
    if (size == 0 || !facePixels) {
        return fallbackTextureId;
    }
    if (static_cast<size_t>(size) > (std::numeric_limits<size_t>::max)() / 4u) {
        return fallbackTextureId;
    }

    std::array<Image, 6> images{};
    std::array<std::vector<uint8_t>, 6> ownedPixels;
    const size_t rowPitch = static_cast<size_t>(size) * 4u;
    if (rowPitch > (std::numeric_limits<size_t>::max)() / static_cast<size_t>(size)) {
        return fallbackTextureId;
    }
    const size_t slicePitch = rowPitch * static_cast<size_t>(size);
    for (size_t face = 0; face < images.size(); ++face) {
        if (!facePixels[face]) {
            return fallbackTextureId;
        }
        try {
            ownedPixels[face].assign(facePixels[face], facePixels[face] + slicePitch);
        } catch (const std::exception&) {
            return fallbackTextureId;
        }
        images[face].width = size;
        images[face].height = size;
        images[face].format = DXGI_FORMAT_R8G8B8A8_UNORM;
        images[face].rowPitch = rowPitch;
        images[face].slicePitch = slicePitch;
        images[face].pixels = ownedPixels[face].data();
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

void TextureManager::UpdateCubeFromRgbaPixels(uint32_t textureId, uint32_t size,
                                              const uint8_t* const* facePixels) {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    if (size == 0 || !facePixels || !IsValidTextureId(textureId) || !IsCubeTextureId(textureId)) {
        return;
    }
    Texture& texture = state_->textures[textureId].texture;
    if (!IsCubeTextureUpdateTargetValid(texture, size)) {
        return;
    }

    size_t rowPitch = 0;
    size_t slicePitch = 0;
    if (!TryResolveRgbaTexturePitches(size, size, rowPitch, slicePitch)) {
        return;
    }

    std::array<D3D12_SUBRESOURCE_DATA, 6> subresources{};
    if (!TryBuildCubeSubresources(facePixels, rowPitch, slicePitch, subresources)) {
        return;
    }

    UploadTextureSubresources(this, dxCommon_, *state_, textureId, texture, subresources.data(),
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
    std::vector<uint8_t> ownedPixels;
    try {
        ownedPixels.assign(pixels, pixels + rowPitch * height);
    } catch (const std::exception&) {
        return fallbackTextureId;
    }
    image.width = width;
    image.height = height;
    image.format = format;
    image.rowPitch = rowPitch;
    image.slicePitch = rowPitch * height;
    image.pixels = ownedPixels.data();

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
    D3D12_RESOURCE_DESC textureDesc = texture.resource->GetDesc();
    if (!IsTexture2DUpdateTargetValid(texture, textureDesc)) {
        return;
    }

    D3D12_SUBRESOURCE_DATA subresource{};
    if (!TryBuildTexture2DSubresource(texture, textureDesc, pixels, rowPitch, subresource)) {
        return;
    }

    UploadTextureSubresources(this, dxCommon_, *state_, textureId, texture, &subresource, 1);
}

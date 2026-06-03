#include "texture/TextureManager.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/SrvManager.h"
#include "texture/Texture.h"
#include <limits>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {

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

} // namespace

uint32_t TextureManager::CreateFromRgbaPixels(uint32_t width, uint32_t height,
                                               const uint8_t *pixels) {
    return CreateTexture2D(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, pixels,
                           static_cast<size_t>(width) * 4u);
}

uint32_t TextureManager::CreateTexture2D(uint32_t width, uint32_t height,
                                         DXGI_FORMAT format,
                                         const uint8_t *pixels,
                                         size_t rowPitch) {
    const uint32_t fallbackTextureId =
        IsValidTextureId(whiteTextureId_) ? whiteTextureId_ : UINT32_MAX;
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
    if (static_cast<size_t>(width) >
        ((std::numeric_limits<size_t>::max)() - 7u) / bitsPerPixel) {
        return fallbackTextureId;
    }
    const size_t minimumRowPitch =
        (static_cast<size_t>(width) * bitsPerPixel + 7u) / 8u;
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
    image.pixels = const_cast<uint8_t *>(pixels);

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

void TextureManager::UpdateTexture2D(uint32_t textureId, const uint8_t *pixels,
                                     size_t rowPitch) {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    if (!pixels || rowPitch == 0 || !IsValidTextureId(textureId)) {
        return;
    }

    Texture &texture = textures_[textureId].texture;
    if (!texture.resource || texture.width <= 0 || texture.height <= 0) {
        return;
    }

    D3D12_RESOURCE_DESC textureDesc = texture.resource->GetDesc();
    if (textureDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        textureDesc.DepthOrArraySize != 1 || textureDesc.MipLevels != 1) {
        return;
    }
    if (textureDesc.Width >
            static_cast<UINT64>((std::numeric_limits<int>::max)()) ||
        textureDesc.Height >
            static_cast<UINT>((std::numeric_limits<int>::max)()) ||
        static_cast<int>(textureDesc.Width) != texture.width ||
        static_cast<int>(textureDesc.Height) != texture.height) {
        return;
    }
    const size_t bitsPerPixel = DirectX::BitsPerPixel(textureDesc.Format);
    if (bitsPerPixel == 0) {
        return;
    }
    if (DirectX::IsCompressed(textureDesc.Format) ||
        DirectX::IsDepthStencil(textureDesc.Format)) {
        return;
    }
    const size_t width = static_cast<size_t>(texture.width);
    if (width >
        ((std::numeric_limits<size_t>::max)() - 7u) / bitsPerPixel) {
        return;
    }
    const size_t expectedRowPitch = (width * bitsPerPixel + 7u) / 8u;
    if (rowPitch < expectedRowPitch) {
        return;
    }

    D3D12_SUBRESOURCE_DATA subresource{};
    subresource.pData = pixels;
    if (rowPitch >
        static_cast<size_t>((std::numeric_limits<LONG_PTR>::max)())) {
        return;
    }
    if (rowPitch > (std::numeric_limits<size_t>::max)() /
                       static_cast<size_t>(texture.height)) {
        return;
    }
    const size_t slicePitch = rowPitch * static_cast<size_t>(texture.height);
    if (slicePitch >
        static_cast<size_t>((std::numeric_limits<LONG_PTR>::max)())) {
        return;
    }
    subresource.RowPitch = static_cast<LONG_PTR>(rowPitch);
    subresource.SlicePitch = static_cast<LONG_PTR>(slicePitch);

    const bool ownsUploadPass = !dxCommon_->IsCommandListRecording();
    if (ownsUploadPass && !dxCommon_->BeginUpload()) {
        return;
    }
    UploadPassScope uploadPass(dxCommon_, this, ownsUploadPass);
    if (!dxCommon_->IsCommandListRecording()) {
        return;
    }

    const UINT frameIndex = dxCommon_->GetBackBufferIndex();
    if (frameIndex < frameUploadBuffers_.size() &&
        lastDynamicUploadFrameIndex_ != frameIndex) {
        frameUploadBuffers_[frameIndex].clear();
        lastDynamicUploadFrameIndex_ = frameIndex;
    }

    const UINT64 uploadSize =
        GetRequiredIntermediateSize(texture.resource.Get(), 0, 1);
    if (uploadSize == 0) {
        return;
    }

    ComPtr<ID3D12Resource> uploadBuffer;
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    if (!GpuResourceHelpers::CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &uploadHeap, D3D12_HEAP_FLAG_NONE,
            &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
            uploadBuffer.GetAddressOf())) {
        return;
    }

    ID3D12GraphicsCommandList *cmdList = dxCommon_->GetCommandList();
    if (cmdList == nullptr) {
        return;
    }

    try {
        if (frameIndex < frameUploadBuffers_.size()) {
            frameUploadBuffers_[frameIndex].push_back(uploadBuffer);
        } else {
            uploadBuffers_.push_back(uploadBuffer);
        }
    } catch (...) {
        return;
    }

    const D3D12_RESOURCE_STATES previousState = texture.state;
    bool rollbackRegistered = false;
    auto registerStateRollback = [&]() -> bool {
        if (rollbackRegistered || !dxCommon_->IsCommandListRecording()) {
            return true;
        }
        if (!dxCommon_->RegisterFrameRollback(
            this,
            [this, textureId, previousState]() {
                if (textureId < textures_.size()) {
                    textures_[textureId].texture.state = previousState;
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
        UpdateSubresources(cmdList, texture.resource.Get(), uploadBuffer.Get(),
                           0, 0, 1, &subresource);
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

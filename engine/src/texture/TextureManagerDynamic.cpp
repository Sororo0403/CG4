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



uint32_t TextureManager::CreateFromRgbaPixels(uint32_t width, uint32_t height,
                                              const uint8_t *pixels) {
    return CreateTexture2D(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, pixels,
                           static_cast<size_t>(width) * 4u);
}

uint32_t TextureManager::CreateTexture2D(uint32_t width, uint32_t height,
                                         DXGI_FORMAT format,
                                         const uint8_t *pixels,
                                         size_t rowPitch) {
    if (width == 0 || height == 0 || !pixels || rowPitch == 0) {
        throw std::runtime_error("CreateTexture2D received invalid pixel data");
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
    if (!dxCommon_ || !dxCommon_->IsCommandListRecording()) {
        return;
    }
    if (!pixels || rowPitch == 0 || textureId >= textures_.size()) {
        throw std::runtime_error("UpdateTexture2D received invalid input");
    }

    Texture &texture = textures_[textureId].texture;
    if (!texture.resource || texture.width <= 0 || texture.height <= 0) {
        throw std::runtime_error("UpdateTexture2D target texture is invalid");
    }

    const UINT frameIndex = dxCommon_->GetBackBufferIndex();
    if (frameIndex < frameUploadBuffers_.size()) {
        frameUploadBuffers_[frameIndex].clear();
    }

    D3D12_RESOURCE_DESC textureDesc = texture.resource->GetDesc();
    const size_t expectedRowPitch =
        static_cast<size_t>(texture.width) *
        DirectX::BitsPerPixel(textureDesc.Format) / 8u;
    if (rowPitch < expectedRowPitch) {
        throw std::runtime_error("UpdateTexture2D rowPitch is too small");
    }

    D3D12_SUBRESOURCE_DATA subresource{};
    subresource.pData = pixels;
    subresource.RowPitch = static_cast<LONG_PTR>(rowPitch);
    subresource.SlicePitch = static_cast<LONG_PTR>(
        rowPitch * static_cast<size_t>(texture.height));

    const UINT64 uploadSize =
        GetRequiredIntermediateSize(texture.resource.Get(), 0, 1);

    ComPtr<ID3D12Resource> uploadBuffer;
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&uploadBuffer)),
                  "Create texture update upload buffer failed");

    ID3D12GraphicsCommandList *cmdList = dxCommon_->GetCommandList();
    auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.resource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->ResourceBarrier(1, &toCopyDest);

    UpdateSubresources(cmdList, texture.resource.Get(), uploadBuffer.Get(), 0,
                       0, 1, &subresource);

    auto toShaderResource = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &toShaderResource);

    if (frameIndex < frameUploadBuffers_.size()) {
        frameUploadBuffers_[frameIndex].push_back(uploadBuffer);
    } else {
        uploadBuffers_.push_back(uploadBuffer);
    }
}

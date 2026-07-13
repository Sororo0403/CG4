#include "texture/CpuTextureLoader.h"

#include "core/PathUtils.h"
#include "internal/TextureManagerDecoding.h"
#include "texture/TextureLimits.h"

#include <DirectXTex.h>
#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <new>
#include <system_error>
#include <utility>

namespace {

bool CopyRgbaImage(const DirectX::Image& source, CpuTextureRgbaImage& destination) {
    if (source.pixels == nullptr || source.width == 0u || source.height == 0u ||
        source.width > (std::numeric_limits<uint32_t>::max)() ||
        source.height > (std::numeric_limits<uint32_t>::max)()) {
        return false;
    }

    const size_t rowBytes = source.width * 4u;
    if (source.rowPitch < rowBytes || rowBytes > (std::numeric_limits<uint32_t>::max)() ||
        source.height > (std::numeric_limits<size_t>::max)() / rowBytes) {
        return false;
    }

    CpuTextureRgbaImage result{};
    result.width = static_cast<uint32_t>(source.width);
    result.height = static_cast<uint32_t>(source.height);
    result.rowPitch = static_cast<uint32_t>(rowBytes);
    try {
        result.pixels.resize(rowBytes * source.height);
    } catch (const std::exception&) {
        return false;
    }

    for (size_t y = 0u; y < source.height; ++y) {
        const uint8_t* srcRow = source.pixels + y * source.rowPitch;
        uint8_t* dstRow = result.pixels.data() + y * rowBytes;
        std::memcpy(dstRow, srcRow, rowBytes);
    }

    destination = std::move(result);
    return true;
}

bool ExistsNoThrow(const std::filesystem::path& path) {
    std::error_code ec;
    try {
        return std::filesystem::is_regular_file(path, ec);
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace

namespace CpuTextureLoader {

bool LoadRgba8FromFile(const std::wstring& filePath, CpuTextureRgbaImage& image) {
    image = {};
    if (filePath.empty()) {
        return false;
    }

    std::filesystem::path resolvedPath;
    try {
        resolvedPath = PathUtils::ResolveAssetPath(filePath);
    } catch (const std::exception&) {
        return false;
    }
    if (resolvedPath.empty() || !ExistsNoThrow(resolvedPath) ||
        !TextureLimits::IsFileWithinInputBudget(resolvedPath)) {
        return false;
    }

    DirectX::TexMetadata metadata{};
    DirectX::ScratchImage source;
    if (!TextureManagerDecoding::DecodeFileForRawPixels(resolvedPath, source, metadata)) {
        return false;
    }

    const DirectX::Image* baseImage = source.GetImage(0, 0, 0);
    if (baseImage == nullptr || baseImage->pixels == nullptr) {
        return false;
    }

    DirectX::ScratchImage rgbaImage;
    const DirectX::Image* rgba = baseImage;
    if (baseImage->format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        if (FAILED(DirectX::Convert(*baseImage, DXGI_FORMAT_R8G8B8A8_UNORM,
                                    DirectX::TEX_FILTER_DEFAULT, 0.0f, rgbaImage)) ||
            !TextureLimits::AreImagesWithinDecodedBudget(rgbaImage.GetImages(),
                                                         rgbaImage.GetImageCount())) {
            return false;
        }
        rgba = rgbaImage.GetImage(0, 0, 0);
    }

    return rgba != nullptr && CopyRgbaImage(*rgba, image);
}

} // namespace CpuTextureLoader

#pragma once

#include <DirectXTex.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>

namespace TextureLimits {

inline constexpr std::uintmax_t kMaxFileBytes = 512ull * 1024ull * 1024ull;
inline constexpr std::size_t kMaxMemoryBytes = 512ull * 1024ull * 1024ull;
inline constexpr std::size_t kMaxDecodedBytes = 1024ull * 1024ull * 1024ull;
inline constexpr std::size_t kMaxDimension = 16384u;
inline constexpr std::size_t kMaxBasePixels = 268435456u;
inline constexpr std::size_t kMaxArraySize = 2048u;
inline constexpr std::size_t kMaxMipLevels = 32u;
inline constexpr std::size_t kMaxImageCount = 4096u;

inline bool IsFileWithinInputBudget(const std::filesystem::path& path) {
    std::error_code ec;
    try {
        const std::uintmax_t size = std::filesystem::file_size(path, ec);
        return !ec && size <= kMaxFileBytes;
    } catch (const std::exception&) {
        return false;
    }
}

inline bool IsMemoryWithinInputBudget(std::size_t size) noexcept {
    return size > 0 && size <= kMaxMemoryBytes;
}

inline bool IsMetadataWithinBudget(const DirectX::TexMetadata& metadata,
                                   std::size_t imageCount = 0) noexcept {
    if (metadata.width == 0 || metadata.height == 0 || metadata.arraySize == 0 ||
        metadata.mipLevels == 0) {
        return false;
    }
    if (metadata.width > kMaxDimension || metadata.height > kMaxDimension ||
        metadata.arraySize > kMaxArraySize || metadata.mipLevels > kMaxMipLevels) {
        return false;
    }
    if (metadata.width > kMaxBasePixels / metadata.height) {
        return false;
    }
    if (metadata.arraySize > (std::numeric_limits<std::size_t>::max)() / metadata.mipLevels) {
        return false;
    }
    const std::size_t expectedImageCount =
        static_cast<std::size_t>(metadata.arraySize) * static_cast<std::size_t>(metadata.mipLevels);
    if (expectedImageCount > kMaxImageCount) {
        return false;
    }
    return imageCount == 0 || imageCount == expectedImageCount;
}

inline bool AreImagesWithinDecodedBudget(const DirectX::Image* images,
                                         std::size_t imageCount) noexcept {
    if (images == nullptr || imageCount == 0 || imageCount > kMaxImageCount) {
        return false;
    }

    std::size_t totalBytes = 0;
    for (std::size_t index = 0; index < imageCount; ++index) {
        const DirectX::Image& image = images[index];
        if (image.pixels == nullptr || image.rowPitch == 0 || image.slicePitch == 0) {
            return false;
        }
        if (image.slicePitch > kMaxDecodedBytes ||
            totalBytes > kMaxDecodedBytes - image.slicePitch) {
            return false;
        }
        totalBytes += image.slicePitch;
    }
    return true;
}

} // namespace TextureLimits

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct CpuTextureRgbaImage {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t rowPitch = 0;
    std::vector<uint8_t> pixels;

    bool IsValid() const noexcept {
        return width > 0u && height > 0u && rowPitch >= width * 4u &&
               pixels.size() >= static_cast<size_t>(rowPitch) * height;
    }
};

namespace CpuTextureLoader {

bool LoadRgba8FromFile(const std::wstring &filePath,
                       CpuTextureRgbaImage &image);

} // namespace CpuTextureLoader

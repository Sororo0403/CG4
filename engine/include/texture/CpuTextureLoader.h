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
        if (width == 0u || height == 0u) {
            return false;
        }
        const size_t minimumRowPitch = static_cast<size_t>(width) * 4u;
        const size_t rowBytes = static_cast<size_t>(rowPitch);
        return rowBytes >= minimumRowPitch &&
               pixels.size() >= rowBytes * static_cast<size_t>(height);
    }
};

namespace CpuTextureLoader {

bool LoadRgba8FromFile(const std::wstring& filePath, CpuTextureRgbaImage& image);

} // namespace CpuTextureLoader

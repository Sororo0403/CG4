#pragma once

#include "TextureManagerAsyncState.h"

#include <DirectXTex.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace TextureManagerDecoding {

enum class TextureColorSpacePolicy {
    Auto,
    Srgb,
    Linear,
};

bool DecodeFileForLoad(const std::filesystem::path& resolvedPath, DirectX::ScratchImage& scratch,
                       DirectX::TexMetadata& metadata,
                       TextureColorSpacePolicy colorSpace = TextureColorSpacePolicy::Auto);
bool DecodeFileForRawPixels(const std::filesystem::path& resolvedPath,
                            DirectX::ScratchImage& scratch, DirectX::TexMetadata& metadata);
bool DecodeMemoryForLoad(const uint8_t* data, size_t size, DirectX::ScratchImage& scratch,
                         DirectX::TexMetadata& metadata,
                         TextureColorSpacePolicy colorSpace = TextureColorSpacePolicy::Auto);
TextureManagerDecodedTexture DecodeResolvedFileForAsync(const std::wstring& resolvedPathText);

} // namespace TextureManagerDecoding

#include "internal/TextureManagerDecoding.h"

#include "core/ComInitialization.h"
#include "core/PathUtils.h"
#include "texture/TextureLimits.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <exception>
#include <initializer_list>
#include <new>
#include <string>
#include <string_view>

namespace {

bool IsDdsTexturePath(const std::filesystem::path& path) {
    try {
        const std::wstring ext = path.extension().wstring();
        return _wcsicmp(ext.c_str(), L".dds") == 0;
    } catch (const std::exception&) {
        return false;
    }
}

std::wstring Lowercase(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return text;
}

bool ContainsAny(const std::wstring& text, std::initializer_list<std::wstring_view> tokens) {
    return std::any_of(tokens.begin(), tokens.end(), [&text](std::wstring_view token) {
        return text.find(token) != std::wstring::npos;
    });
}

bool IsLikelyLinearDataTexture(const std::filesystem::path& path) {
    try {
        const std::wstring text = Lowercase(path.filename().wstring());
        return ContainsAny(text, {L"normal",    L"_nor",   L"-nor",         L"nrm",
                                  L"roughness", L"_rough", L"-rough",       L"metallic",
                                  L"metalness", L"_metal", L"-metal",       L"ambientocclusion",
                                  L"occlusion", L"_ao",    L"-ao",          L"_arm",
                                  L"-arm",      L"_orm",   L"-orm",         L"opacity",
                                  L"alpha",     L"height", L"displacement", L"mask",
                                  L"scattering"});
    } catch (const std::exception&) {
        return false;
    }
}

bool ShouldUseSrgb(const std::filesystem::path& path,
                   TextureManagerDecoding::TextureColorSpacePolicy colorSpace) {
    struct ColorSpaceDecision {
        TextureManagerDecoding::TextureColorSpacePolicy policy;
        bool useSrgb;
    };
    static constexpr std::array<ColorSpaceDecision, 2> kExplicitDecisions{{
        {TextureManagerDecoding::TextureColorSpacePolicy::Srgb, true},
        {TextureManagerDecoding::TextureColorSpacePolicy::Linear, false},
    }};

    const auto it = std::find_if(
        kExplicitDecisions.begin(), kExplicitDecisions.end(),
        [colorSpace](const ColorSpaceDecision& entry) { return entry.policy == colorSpace; });
    return it != kExplicitDecisions.end() ? it->useSrgb : !IsLikelyLinearDataTexture(path);
}

DXGI_FORMAT MakeSrgbFormat(DXGI_FORMAT format) {
    struct SrgbFormatMap {
        DXGI_FORMAT linear;
        DXGI_FORMAT srgb;
    };
    static constexpr std::array<SrgbFormatMap, 7> kSrgbFormats{{
        {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB},
        {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB},
        {DXGI_FORMAT_B8G8R8X8_UNORM, DXGI_FORMAT_B8G8R8X8_UNORM_SRGB},
        {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM_SRGB},
        {DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC2_UNORM_SRGB},
        {DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM_SRGB},
        {DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_BC7_UNORM_SRGB},
    }};

    const auto it =
        std::find_if(kSrgbFormats.begin(), kSrgbFormats.end(),
                     [format](const SrgbFormatMap& entry) { return entry.linear == format; });
    return it != kSrgbFormats.end() ? it->srgb : format;
}

bool DecodeResolvedTextureFile(const std::filesystem::path& resolvedPath,
                               DirectX::ScratchImage& scratch, DirectX::TexMetadata& metadata,
                               TextureManagerDecoding::TextureColorSpacePolicy colorSpace) {
    try {
        std::error_code ec;
        if (!std::filesystem::exists(resolvedPath, ec) ||
            !TextureLimits::IsFileWithinInputBudget(resolvedPath)) {
            return false;
        }

        const bool useSrgb = ShouldUseSrgb(resolvedPath, colorSpace);
        if (IsDdsTexturePath(resolvedPath)) {
            if (FAILED(DirectX::GetMetadataFromDDSFile(resolvedPath.c_str(),
                                                       DirectX::DDS_FLAGS_NONE, metadata)) ||
                !TextureLimits::IsMetadataWithinBudget(metadata)) {
                return false;
            }
            if (FAILED(DirectX::LoadFromDDSFile(resolvedPath.c_str(), DirectX::DDS_FLAGS_NONE,
                                                &metadata, scratch))) {
                return false;
            }
            if (useSrgb) {
                metadata.format = MakeSrgbFormat(metadata.format);
            }
            return true;
        }

        ScopedComInitialization com;
        if (!com.IsUsable()) {
            return false;
        }
        const DirectX::WIC_FLAGS wicFlags =
            useSrgb ? DirectX::WIC_FLAGS_FORCE_SRGB : DirectX::WIC_FLAGS_IGNORE_SRGB;
        if (FAILED(DirectX::GetMetadataFromWICFile(resolvedPath.c_str(), wicFlags, metadata)) ||
            !TextureLimits::IsMetadataWithinBudget(metadata)) {
            return false;
        }
        return SUCCEEDED(
            DirectX::LoadFromWICFile(resolvedPath.c_str(), wicFlags, &metadata, scratch));
    } catch (const std::exception&) {
        return false;
    }
}

bool HasDecodedImagesWithinBudget(const DirectX::ScratchImage& scratch,
                                  const DirectX::TexMetadata& metadata) {
    return scratch.GetImages() != nullptr && scratch.GetImageCount() > 0 &&
           TextureLimits::IsMetadataWithinBudget(metadata, scratch.GetImageCount()) &&
           TextureLimits::AreImagesWithinDecodedBudget(scratch.GetImages(),
                                                       scratch.GetImageCount());
}

} // namespace

namespace TextureManagerDecoding {

bool DecodeFileForLoad(const std::filesystem::path& resolvedPath, DirectX::ScratchImage& scratch,
                       DirectX::TexMetadata& metadata, TextureColorSpacePolicy colorSpace) {
    return DecodeResolvedTextureFile(resolvedPath, scratch, metadata, colorSpace) &&
           HasDecodedImagesWithinBudget(scratch, metadata);
}

bool DecodeFileForRawPixels(const std::filesystem::path& resolvedPath,
                            DirectX::ScratchImage& scratch, DirectX::TexMetadata& metadata) {
    return DecodeResolvedTextureFile(resolvedPath, scratch, metadata,
                                     TextureColorSpacePolicy::Linear) &&
           HasDecodedImagesWithinBudget(scratch, metadata);
}

bool DecodeMemoryForLoad(const uint8_t* data, size_t size, DirectX::ScratchImage& scratch,
                         DirectX::TexMetadata& metadata, TextureColorSpacePolicy colorSpace) {
    if (!data || !TextureLimits::IsMemoryWithinInputBudget(size)) {
        return false;
    }

    ScopedComInitialization com;
    if (!com.IsUsable()) {
        return false;
    }
    const bool useSrgb = colorSpace != TextureColorSpacePolicy::Linear;
    const DirectX::WIC_FLAGS wicFlags =
        useSrgb ? DirectX::WIC_FLAGS_FORCE_SRGB : DirectX::WIC_FLAGS_IGNORE_SRGB;
    if (FAILED(DirectX::GetMetadataFromWICMemory(data, size, wicFlags, metadata)) ||
        !TextureLimits::IsMetadataWithinBudget(metadata)) {
        return false;
    }
    if (FAILED(DirectX::LoadFromWICMemory(data, size, wicFlags, &metadata, scratch))) {
        return false;
    }
    return HasDecodedImagesWithinBudget(scratch, metadata);
}

TextureManagerDecodedTexture DecodeResolvedFileForAsync(const std::wstring& resolvedPathText) {
    try {
        const std::filesystem::path resolvedPath(resolvedPathText);
        TextureManagerDecodedTexture decoded{};
        decoded.pathKey = PathUtils::NormalizePathKey(resolvedPath);
        if (decoded.pathKey.empty()) {
            return decoded;
        }

        decoded.succeeded =
            DecodeResolvedTextureFile(resolvedPath, decoded.scratch, decoded.metadata,
                                      TextureColorSpacePolicy::Auto) &&
            HasDecodedImagesWithinBudget(decoded.scratch, decoded.metadata);
        if (!decoded.succeeded) {
            return {};
        }
        return decoded;
    } catch (const std::exception&) {
        return {};
    }
}

} // namespace TextureManagerDecoding

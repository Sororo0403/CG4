#include "internal/AssimpMeshLoaderMaterial.h"

#include "core/Numeric.h"
#include "model/ModelLimits.h"
#include "texture/TextureLimits.h"
#include "texture/TextureManager.h"

#include <DirectXMath.h>
#include <algorithm>
#include <assimp/GltfMaterial.h>
#include <charconv>
#include <exception>
#include <filesystem>
#include <limits>
#include <new>
#include <vector>

using namespace DirectX;

namespace AssimpMeshLoaderMaterial {
namespace {

using Numeric::ClampFinite;

bool TryParseEmbeddedTextureIndex(const std::string& name, unsigned int& index) {
    if (name.size() <= 1 || name[0] != '*') {
        return false;
    }

    unsigned int parsed = 0;
    const char* begin = name.data() + 1;
    const char* end = name.data() + name.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }

    index = parsed;
    return true;
}

bool IsLoadedMaterialTexture(const TextureManager& textureManager, uint32_t textureId) {
    return textureManager.IsValidTextureId(textureId) &&
           textureId != textureManager.GetWhiteTextureId();
}

bool IsSrgbMaterialTexture(aiTextureType textureType) {
    return textureType == aiTextureType_BASE_COLOR || textureType == aiTextureType_DIFFUSE ||
           textureType == aiTextureType_EMISSIVE;
}

bool TryLoadCompressedEmbeddedTexture(TextureManager& textureManager, const aiTexture& texture,
                                      aiTextureType textureType, uint32_t& outTextureId) {
    if (texture.mWidth == 0 || texture.pcData == nullptr ||
        texture.mWidth > ModelLimits::kMaxEmbeddedTextureBytes) {
        return false;
    }

    outTextureId = IsSrgbMaterialTexture(textureType)
                       ? textureManager.LoadFromMemorySrgb(
                             reinterpret_cast<const uint8_t*>(texture.pcData), texture.mWidth)
                       : textureManager.LoadFromMemoryLinear(
                             reinterpret_cast<const uint8_t*>(texture.pcData), texture.mWidth);
    return IsLoadedMaterialTexture(textureManager, outTextureId);
}

bool TryGetEmbeddedPixelCount(const aiTexture& texture, size_t& pixelCount) {
    if (texture.mWidth == 0 || texture.mHeight == 0 || texture.pcData == nullptr) {
        return false;
    }
    if (texture.mWidth > TextureLimits::kMaxDimension ||
        texture.mHeight > TextureLimits::kMaxDimension) {
        return false;
    }
    if (static_cast<size_t>(texture.mWidth) >
        (std::numeric_limits<size_t>::max)() / static_cast<size_t>(texture.mHeight)) {
        return false;
    }

    pixelCount = static_cast<size_t>(texture.mWidth) * static_cast<size_t>(texture.mHeight);
    return pixelCount <= ModelLimits::kMaxEmbeddedTexturePixels &&
           pixelCount <= (std::numeric_limits<size_t>::max)() / 4u &&
           pixelCount <= ModelLimits::kMaxEmbeddedTextureBytes / 4u;
}

bool CopyEmbeddedRgbaPixels(const aiTexture& texture, size_t pixelCount,
                            std::vector<uint8_t>& pixels) {
    try {
        pixels.resize(pixelCount * 4u);
    } catch (const std::exception&) {
        return false;
    }

    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const aiTexel& src = texture.pcData[pixelIndex];
        const size_t dst = pixelIndex * 4u;
        pixels[dst + 0u] = src.r;
        pixels[dst + 1u] = src.g;
        pixels[dst + 2u] = src.b;
        pixels[dst + 3u] = src.a;
    }
    return true;
}

bool TryLoadRawEmbeddedTexture(TextureManager& textureManager, const aiTexture& texture,
                               aiTextureType textureType, uint32_t& outTextureId) {
    size_t pixelCount = 0;
    if (!TryGetEmbeddedPixelCount(texture, pixelCount)) {
        return false;
    }

    std::vector<uint8_t> pixels;
    if (!CopyEmbeddedRgbaPixels(texture, pixelCount, pixels)) {
        return false;
    }

    outTextureId =
        IsSrgbMaterialTexture(textureType)
            ? textureManager.CreateFromRgbaPixelsSrgb(texture.mWidth, texture.mHeight,
                                                      pixels.data())
            : textureManager.CreateFromRgbaPixels(texture.mWidth, texture.mHeight, pixels.data());
    return IsLoadedMaterialTexture(textureManager, outTextureId);
}

bool TryLoadEmbeddedTexture(TextureManager& textureManager, const aiScene& scene,
                            const std::string& texName, aiTextureType textureType,
                            uint32_t& outTextureId) {
    unsigned int texIndex = 0;
    if (!TryParseEmbeddedTextureIndex(texName, texIndex) || texIndex >= scene.mNumTextures ||
        scene.mTextures == nullptr) {
        return false;
    }

    const aiTexture* tex = scene.mTextures[texIndex];
    if (!tex) {
        return false;
    }

    if (tex->mHeight == 0) {
        return TryLoadCompressedEmbeddedTexture(textureManager, *tex, textureType, outTextureId);
    }

    return TryLoadRawEmbeddedTexture(textureManager, *tex, textureType, outTextureId);
}

bool TryLoadMaterialTexture(TextureManager& textureManager, const aiScene& scene,
                            const aiMaterial& material, const std::string& modelPath,
                            aiTextureType textureType, uint32_t& outTextureId) {
    aiString texPath;
    if (material.GetTexture(textureType, 0, &texPath) != AI_SUCCESS) {
        return false;
    }

    const std::string texName = texPath.C_Str();
    if (!texName.empty() && texName[0] == '*') {
        return TryLoadEmbeddedTexture(textureManager, scene, texName, textureType, outTextureId);
    }

    try {
        const std::filesystem::path modelFilePath(modelPath);
        const std::filesystem::path fullPath = modelFilePath.parent_path() / texName;
        outTextureId = IsSrgbMaterialTexture(textureType)
                           ? textureManager.LoadSrgb(fullPath.wstring())
                           : textureManager.LoadLinear(fullPath.wstring());
    } catch (const std::exception&) {
        return false;
    }
    return IsLoadedMaterialTexture(textureManager, outTextureId);
}

} // namespace

void ApplyMaterialBaseColor(const MaterialSource& source, Material& material) {
    aiColor4D baseColor;
    if (source.material &&
        aiGetMaterialColor(source.material, AI_MATKEY_BASE_COLOR, &baseColor) == AI_SUCCESS) {
        material.color.x = ClampFinite(baseColor.r, 0.0f, 1.0f, 1.0f);
        material.color.y = ClampFinite(baseColor.g, 0.0f, 1.0f, 1.0f);
        material.color.z = ClampFinite(baseColor.b, 0.0f, 1.0f, 1.0f);
        material.color.w = ClampFinite(baseColor.a, 0.0f, 1.0f, 1.0f);
    } else if (source.material && aiGetMaterialColor(source.material, AI_MATKEY_COLOR_DIFFUSE,
                                                     &baseColor) == AI_SUCCESS) {
        material.color.x = ClampFinite(baseColor.r, 0.0f, 1.0f, 1.0f);
        material.color.y = ClampFinite(baseColor.g, 0.0f, 1.0f, 1.0f);
        material.color.z = ClampFinite(baseColor.b, 0.0f, 1.0f, 1.0f);
    }

    float opacity = 1.0f;
    if (source.material && source.material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
        material.color.w = ClampFinite(opacity, 0.0f, 1.0f, 1.0f);
    }
}

void ApplyMaterialTextureBindings(const MaterialSource& source, Material& material) {
    material.enableTexture = source.hasBaseColorTexture ? 1 : 0;
    material.enableNormalMap = source.hasNormalTexture ? 1 : 0;
    material.baseColorTextureId =
        source.hasBaseColorTexture ? source.baseColorTextureId : kInvalidResourceId;
    material.normalTextureId =
        source.hasNormalTexture ? source.normalTextureId : kInvalidResourceId;
    material.roughnessTextureId =
        source.hasRoughnessTexture ? source.roughnessTextureId : kInvalidResourceId;
    material.metallicTextureId =
        source.hasMetallicTexture ? source.metallicTextureId : kInvalidResourceId;
    material.pbrTexturePacking = static_cast<int32_t>(source.pbrTexturePacking);
}

void ApplyMaterialPbrFactors(const MaterialSource& source, Material& material) {
    float roughnessFactor = source.hasRoughnessTexture ? 1.0f : material.roughness;
    if (source.material &&
        source.material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor) == AI_SUCCESS) {
        material.roughness = ClampFinite(roughnessFactor, 0.0f, 1.0f, material.roughness);
    } else if (source.hasRoughnessTexture) {
        material.roughness = 1.0f;
    }

    float metallicFactor = source.hasMetallicTexture ? 1.0f : material.metallic;
    if (source.material &&
        source.material->Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor) == AI_SUCCESS) {
        material.metallic = ClampFinite(metallicFactor, 0.0f, 1.0f, material.metallic);
    } else if (source.hasMetallicTexture) {
        material.metallic = 1.0f;
    }
}

MaterialSource LoadSource(TextureManager& textureManager, const aiScene& scene, const aiMesh& mesh,
                          const std::string& path) {
    MaterialSource source{};
    source.baseColorTextureId = textureManager.GetWhiteTextureId();

    if (!scene.HasMaterials() || mesh.mMaterialIndex >= scene.mNumMaterials) {
        return source;
    }

    source.material = scene.mMaterials[mesh.mMaterialIndex];
    if (source.material == nullptr) {
        return source;
    }
    source.hasBaseColorTexture =
        TryLoadMaterialTexture(textureManager, scene, *source.material, path,
                               aiTextureType_BASE_COLOR, source.baseColorTextureId) ||
        TryLoadMaterialTexture(textureManager, scene, *source.material, path, aiTextureType_DIFFUSE,
                               source.baseColorTextureId);
    source.hasNormalTexture =
        TryLoadMaterialTexture(textureManager, scene, *source.material, path, aiTextureType_NORMALS,
                               source.normalTextureId) ||
        TryLoadMaterialTexture(textureManager, scene, *source.material, path, aiTextureType_HEIGHT,
                               source.normalTextureId);
    uint32_t metallicRoughnessTextureId = kInvalidResourceId;
    const bool hasMetallicRoughnessTexture =
        TryLoadMaterialTexture(textureManager, scene, *source.material, path,
                               aiTextureType_GLTF_METALLIC_ROUGHNESS, metallicRoughnessTextureId);
    source.hasRoughnessTexture =
        hasMetallicRoughnessTexture ||
        TryLoadMaterialTexture(textureManager, scene, *source.material, path,
                               aiTextureType_DIFFUSE_ROUGHNESS, source.roughnessTextureId);
    if (hasMetallicRoughnessTexture) {
        source.roughnessTextureId = metallicRoughnessTextureId;
    }
    source.hasMetallicTexture =
        hasMetallicRoughnessTexture ||
        TryLoadMaterialTexture(textureManager, scene, *source.material, path,
                               aiTextureType_METALNESS, source.metallicTextureId);
    if (hasMetallicRoughnessTexture) {
        source.metallicTextureId = metallicRoughnessTextureId;
        source.pbrTexturePacking = PbrTexturePacking::MetallicRoughness;
    }
    return source;
}

Material BuildMaterial(const MaterialSource& source) {
    Material material{};
    material.color = {1, 1, 1, 1};
    material.reflectionStrength = 0.18f;
    material.reflectionFresnelStrength = 0.12f;

    ApplyMaterialBaseColor(source, material);
    XMStoreFloat4x4(&material.uvTransform, XMMatrixTranspose(XMMatrixIdentity()));
    ApplyMaterialTextureBindings(source, material);
    ApplyMaterialPbrFactors(source, material);
    return material;
}

} // namespace AssimpMeshLoaderMaterial

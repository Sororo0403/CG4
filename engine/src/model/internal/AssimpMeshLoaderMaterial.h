#pragma once

#include "core/ResourceHandle.h"
#include "model/Material.h"

#include <assimp/scene.h>
#include <cstdint>
#include <string>

class TextureManager;

namespace AssimpMeshLoaderMaterial {

struct MaterialSource {
    aiMaterial* material = nullptr;
    uint32_t baseColorTextureId = kInvalidResourceId;
    uint32_t normalTextureId = kInvalidResourceId;
    uint32_t roughnessTextureId = kInvalidResourceId;
    uint32_t metallicTextureId = kInvalidResourceId;
    PbrTexturePacking pbrTexturePacking = PbrTexturePacking::Separate;
    bool hasBaseColorTexture = false;
    bool hasNormalTexture = false;
    bool hasRoughnessTexture = false;
    bool hasMetallicTexture = false;
};

MaterialSource LoadSource(TextureManager& textureManager, const aiScene& scene, const aiMesh& mesh,
                          const std::string& path);
Material BuildMaterial(const MaterialSource& source);

} // namespace AssimpMeshLoaderMaterial

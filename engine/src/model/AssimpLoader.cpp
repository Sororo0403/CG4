#include "model/AssimpLoader.h"
#include "model/ModelLimits.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <filesystem>
#include <limits>

namespace {

bool IsModelFileWithinInputBudget(const std::string &path) {
    std::error_code ec;
    const std::uintmax_t fileSize = std::filesystem::file_size(path, ec);
    return !ec && fileSize <= ModelLimits::kMaxFileBytes;
}

bool AddWithinLimit(std::size_t &total, std::size_t value,
                    std::size_t limit) noexcept {
    if (value > limit || total > limit - value) {
        return false;
    }
    total += value;
    return true;
}

bool IsSceneWithinInputBudget(const aiScene *scene) {
    if (scene == nullptr || !scene->HasMeshes()) {
        return false;
    }
    if (scene->mNumMeshes > ModelLimits::kMaxMeshes ||
        scene->mNumMaterials > ModelLimits::kMaxMaterials ||
        scene->mNumTextures > ModelLimits::kMaxEmbeddedTextures ||
        scene->mNumAnimations > ModelLimits::kMaxAnimations) {
        return false;
    }
    if ((scene->mNumMeshes > 0 && scene->mMeshes == nullptr) ||
        (scene->mNumMaterials > 0 && scene->mMaterials == nullptr) ||
        (scene->mNumTextures > 0 && scene->mTextures == nullptr) ||
        (scene->mNumAnimations > 0 && scene->mAnimations == nullptr)) {
        return false;
    }

    std::size_t totalVertices = 0;
    std::size_t totalFaces = 0;
    std::size_t totalBones = 0;
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes;
         ++meshIndex) {
        const aiMesh *mesh = scene->mMeshes[meshIndex];
        if (mesh == nullptr) {
            continue;
        }
        if (mesh->mNumVertices > ModelLimits::kMaxVerticesPerMesh ||
            mesh->mNumFaces > ModelLimits::kMaxFacesPerMesh ||
            mesh->mNumBones > ModelLimits::kMaxBonesPerMesh) {
            return false;
        }
        if (!AddWithinLimit(totalVertices, mesh->mNumVertices,
                            ModelLimits::kMaxTotalVertices) ||
            !AddWithinLimit(totalFaces, mesh->mNumFaces,
                            ModelLimits::kMaxTotalFaces) ||
            !AddWithinLimit(totalBones, mesh->mNumBones,
                            ModelLimits::kMaxTotalBones)) {
            return false;
        }
    }

    std::size_t totalAnimationChannels = 0;
    std::size_t totalAnimationKeys = 0;
    for (unsigned int animationIndex = 0;
         animationIndex < scene->mNumAnimations; ++animationIndex) {
        const aiAnimation *animation = scene->mAnimations[animationIndex];
        if (animation == nullptr) {
            continue;
        }
        if (!AddWithinLimit(totalAnimationChannels, animation->mNumChannels,
                            ModelLimits::kMaxAnimationChannels)) {
            return false;
        }
        if (animation->mNumChannels > 0 && animation->mChannels == nullptr) {
            return false;
        }
        for (unsigned int channelIndex = 0;
             channelIndex < animation->mNumChannels; ++channelIndex) {
            const aiNodeAnim *channel = animation->mChannels[channelIndex];
            if (channel == nullptr) {
                continue;
            }
            const std::size_t keyCount =
                static_cast<std::size_t>(channel->mNumPositionKeys) +
                static_cast<std::size_t>(channel->mNumRotationKeys) +
                static_cast<std::size_t>(channel->mNumScalingKeys);
            if (keyCount > ModelLimits::kMaxAnimationKeysPerChannel ||
                !AddWithinLimit(totalAnimationKeys, keyCount,
                                ModelLimits::kMaxAnimationKeysTotal)) {
                return false;
            }
        }
    }

    return true;
}

} // namespace

void AssimpLoader::Initialize(TextureManager *textureManager,
                              MeshManager *meshManager,
                              MaterialManager *materialManager) {
    meshLoader_.Initialize(textureManager, meshManager, materialManager);
}

Model AssimpLoader::Load(const std::string &path) {
    if (!meshLoader_.IsInitialized()) {
        return {};
    }
    if (!IsModelFileWithinInputBudget(path)) {
        return {};
    }

    Assimp::Importer importer;

    const aiScene *scene = importer.ReadFile(
        path, aiProcess_Triangulate | aiProcess_FlipUVs |
                  aiProcess_JoinIdenticalVertices |
                  aiProcess_LimitBoneWeights | aiProcess_CalcTangentSpace);

    if (!IsSceneWithinInputBudget(scene)) {
        return {};
    }

    Model model{};
    if (scene->mRootNode) {
        model.rootNodeName = scene->mRootNode->mName.C_Str();
    }
    meshLoader_.LoadMeshes(scene, path, model);
    animationLoader_.LoadAnimations(scene, model);

    model.finalBoneMatrices.resize(model.bones.size());
    return model;
}

#include "AssimpMeshLoader.h"
#include "Material.h"
#include "MaterialManager.h"
#include "MeshManager.h"
#include "TextureManager.h"
#include "Vertex.h"
#include <DirectXMath.h>
#include <assimp/GltfMaterial.h>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <vector>

using namespace DirectX;

namespace {

XMFLOAT4X4 ToMatrix(const aiMatrix4x4 &m) {
    return {m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2,
            m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4};
}

} // namespace

void AssimpMeshLoader::Initialize(TextureManager *textureManager,
                                  MeshManager *meshManager,
                                  MaterialManager *materialManager) {
    textureManager_ = textureManager;
    meshManager_ = meshManager;
    materialManager_ = materialManager;
}

bool AssimpMeshLoader::IsInitialized() const {
    return textureManager_ && meshManager_ && materialManager_;
}

void AssimpMeshLoader::LoadMeshes(const aiScene *scene, const std::string &path,
                                  Model &model) const {
    if (!IsInitialized()) {
        throw std::runtime_error("AssimpMeshLoader is not initialized");
    }
    if (!scene) {
        return;
    }

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes;
         meshIndex++) {
        aiMesh *mesh = scene->mMeshes[meshIndex];
        if (!mesh) {
            continue;
        }

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        vertices.reserve(mesh->mNumVertices);
        indices.reserve(mesh->mNumFaces * 3);

        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex v{};

            v.position = {mesh->mVertices[i].x, mesh->mVertices[i].y,
                          mesh->mVertices[i].z};
            if (mesh->HasNormals()) {
                v.normal = {mesh->mNormals[i].x, mesh->mNormals[i].y,
                            mesh->mNormals[i].z};
            }

            if (mesh->HasTextureCoords(0)) {
                v.uv = {mesh->mTextureCoords[0][i].x,
                        mesh->mTextureCoords[0][i].y};
            } else {
                v.uv = {0.0f, 0.0f};
            }

            vertices.push_back(v);
        }

        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            const aiFace &face = mesh->mFaces[i];

            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                indices.push_back(face.mIndices[j]);
            }
        }

        if (vertices.empty() || indices.empty()) {
            continue;
        }

        ModelSubMesh subMesh{};
        subMesh.vertexCount = static_cast<uint32_t>(vertices.size());

        if (mesh->HasBones()) {
            for (unsigned int i = 0; i < mesh->mNumBones; i++) {
                aiBone *bone = mesh->mBones[i];

                if (!bone) {
                    continue;
                }

                std::string boneName = bone->mName.C_Str();
                uint32_t boneIndex = 0;

                auto it = model.boneMap.find(boneName);

                if (it == model.boneMap.end()) {
                    boneIndex = static_cast<uint32_t>(model.bones.size());

                    model.boneMap[boneName] = boneIndex;

                    BoneInfo info{};
                    info.name = boneName;
                    // This project's existing animation path already uses
                    // Assimp's node transforms as-is, so the inverse bind pose
                    // also needs to stay in the same space.
                    info.offsetMatrix = ToMatrix(bone->mOffsetMatrix);

                    model.bones.push_back(info);
                } else {
                    boneIndex = it->second;
                }

                JointWeightData &jointWeightData = subMesh.skinClusterData[boneName];
                jointWeightData.inverseBindPoseMatrix =
                    model.bones[boneIndex].offsetMatrix;

                for (unsigned int w = 0; w < bone->mNumWeights; w++) {
                    uint32_t vertexId = bone->mWeights[w].mVertexId;
                    float weight = bone->mWeights[w].mWeight;

                    if (vertexId >= vertices.size()) {
                        continue;
                    }

                    jointWeightData.vertexWeights.push_back({weight, vertexId});
                }
            }
        }

        aiMaterial *mat = nullptr;
        uint32_t textureId = 0;
        bool hasTexture = false;

        if (scene->HasMaterials() &&
            mesh->mMaterialIndex < scene->mNumMaterials) {
            mat = scene->mMaterials[mesh->mMaterialIndex];

            auto tryLoadTexture = [&](aiTextureType textureType) -> bool {
                aiString texPath;
                if (!mat || mat->GetTexture(textureType, 0, &texPath) !=
                                AI_SUCCESS) {
                    return false;
                }

                std::string texName = texPath.C_Str();

                if (!texName.empty() && texName[0] == '*') {
                    int texIndex = std::atoi(texName.c_str() + 1);
                    if (texIndex < 0 ||
                        texIndex >= static_cast<int>(scene->mNumTextures)) {
                        return false;
                    }

                    aiTexture *tex = scene->mTextures[texIndex];
                    if (!tex) {
                        return false;
                    }

                    if (tex->mHeight == 0) {
                        textureId = textureManager_->LoadFromMemory(
                            reinterpret_cast<const uint8_t *>(tex->pcData),
                            tex->mWidth);
                        return true;
                    }

                    return false;
                }

                std::filesystem::path modelPath(path);
                auto fullPath = modelPath.parent_path() / texName;
                textureId = textureManager_->Load(fullPath.wstring());
                return true;
            };

            hasTexture = tryLoadTexture(aiTextureType_BASE_COLOR) ||
                         tryLoadTexture(aiTextureType_DIFFUSE);
        }

        uint32_t meshId = meshManager_->CreateMesh(
            vertices.data(), sizeof(Vertex),
            static_cast<uint32_t>(vertices.size()), indices.data(),
            static_cast<uint32_t>(indices.size()));

        Material material{};
        material.color = {1, 1, 1, 1};
        material.reflectionStrength = 0.18f;
        material.reflectionFresnelStrength = 0.12f;

        aiColor4D diffuse;

        if (mat && aiGetMaterialColor(mat, AI_MATKEY_COLOR_DIFFUSE, &diffuse) ==
                       AI_SUCCESS) {
            material.color.x = diffuse.r;
            material.color.y = diffuse.g;
            material.color.z = diffuse.b;
        }

        float opacity = 1.0f;

        if (mat && mat->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
            material.color.w = opacity;
        }

        XMStoreFloat4x4(&material.uvTransform,
                        XMMatrixTranspose(XMMatrixIdentity()));

        material.enableTexture = hasTexture ? 1 : 0;

        subMesh.meshId = meshId;
        subMesh.textureId = textureId;
        subMesh.materialId = materialManager_->CreateMaterial(material);

        model.subMeshes.push_back(subMesh);
    }

    if (model.subMeshes.empty()) {
        throw std::runtime_error("Mesh is null");
    }

    model.meshId = model.subMeshes[0].meshId;
    model.textureId = model.subMeshes[0].textureId;
    model.materialId = model.subMeshes[0].materialId;

    if (!model.bones.empty()) {
        BuildBoneHierarchy(scene, model);
    }
}

const aiNode *AssimpMeshLoader::FindNodeByName(const aiNode *node,
                                               const std::string &name) const {
    if (!node) {
        return nullptr;
    }

    if (name == node->mName.C_Str()) {
        return node;
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        const aiNode *found = FindNodeByName(node->mChildren[i], name);
        if (found) {
            return found;
        }
    }

    return nullptr;
}

void AssimpMeshLoader::BuildBoneHierarchy(const aiScene *scene, Model &model) const {
    if (!scene || !scene->mRootNode) {
        return;
    }

    for (size_t i = 0; i < model.bones.size(); i++) {
        const std::string &boneName = model.bones[i].name;

        const aiNode *node = FindNodeByName(scene->mRootNode, boneName);
        if (!node) {
            model.bones[i].parentIndex = -1;
            model.bones[i].localBindMatrix = ToMatrix(aiMatrix4x4());
            model.bones[i].parentAdjustmentMatrix = ToMatrix(aiMatrix4x4());
            continue;
        }

        aiMatrix4x4 adjustment{};
        int parentIndex = -1;
        const aiNode *parent = node->mParent;

        while (parent) {
            auto it = model.boneMap.find(parent->mName.C_Str());
            if (it != model.boneMap.end()) {
                parentIndex = static_cast<int>(it->second);
                break;
            }

            adjustment *= parent->mTransformation;
            parent = parent->mParent;
        }

        model.bones[i].parentIndex = parentIndex;
        model.bones[i].parentAdjustmentMatrix = ToMatrix(adjustment);
        model.bones[i].localBindMatrix =
            ToMatrix(node->mTransformation * adjustment);
    }
}

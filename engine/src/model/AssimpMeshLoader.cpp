#include "model/AssimpMeshLoader.h"
#include "model/Material.h"
#include "model/MaterialManager.h"
#include "model/MeshManager.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include <DirectXMath.h>
#include <algorithm>
#include <assimp/GltfMaterial.h>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>
#include <vector>

using namespace DirectX;

namespace {
constexpr float kEpsilon = 0.000001f;

float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

float ClampFinite(float value, float minimum, float maximum, float fallback) {
    return std::clamp(FiniteOr(value, fallback), minimum, maximum);
}

XMFLOAT3 SanitizeFloat3(const aiVector3D &value,
                        const XMFLOAT3 &fallback) {
    return {FiniteOr(value.x, fallback.x), FiniteOr(value.y, fallback.y),
            FiniteOr(value.z, fallback.z)};
}

XMFLOAT3 SanitizeNormal(const aiVector3D &value) {
    XMFLOAT3 normal = SanitizeFloat3(value, {0.0f, 1.0f, 0.0f});
    XMVECTOR vector = XMLoadFloat3(&normal);
    const float lengthSq = XMVectorGetX(XMVector3LengthSq(vector));
    if (!std::isfinite(lengthSq) || lengthSq <= kEpsilon) {
        return {0.0f, 1.0f, 0.0f};
    }
    XMStoreFloat3(&normal, XMVector3Normalize(vector));
    return normal;
}

XMFLOAT4 SanitizeTangent(const aiVector3D &value) {
    XMFLOAT3 tangent = SanitizeFloat3(value, {1.0f, 0.0f, 0.0f});
    XMVECTOR vector = XMLoadFloat3(&tangent);
    const float lengthSq = XMVectorGetX(XMVector3LengthSq(vector));
    if (!std::isfinite(lengthSq) || lengthSq <= kEpsilon) {
        return {1.0f, 0.0f, 0.0f, 1.0f};
    }
    XMStoreFloat3(&tangent, XMVector3Normalize(vector));
    return {tangent.x, tangent.y, tangent.z, 1.0f};
}

XMFLOAT4X4 ToMatrix(const aiMatrix4x4 &m) {
    return {m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2,
            m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4};
}

uint32_t CheckedUint32Size(size_t value, const char *message) {
    (void)message;
    if (value > (std::numeric_limits<uint32_t>::max)()) {
        return UINT32_MAX;
    }
    return static_cast<uint32_t>(value);
}

int CheckedIntSize(size_t value, const char *message) {
    (void)message;
    if (value > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return (std::numeric_limits<int>::max)();
    }
    return static_cast<int>(value);
}

bool TryParseEmbeddedTextureIndex(const std::string &name, unsigned int &index) {
    if (name.size() <= 1 || name[0] != '*') {
        return false;
    }

    unsigned int parsed = 0;
    const char *begin = name.data() + 1;
    const char *end = name.data() + name.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }

    index = parsed;
    return true;
}

} // namespace

void AssimpMeshLoader::Initialize(TextureManager *textureManager,
                                  MeshManager *meshManager,
                                  MaterialManager *materialManager) {
    if (!textureManager || !meshManager || !materialManager) {
        textureManager_ = nullptr;
        meshManager_ = nullptr;
        materialManager_ = nullptr;
        return;
    }

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
        return;
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
        if (!mesh->HasPositions() || mesh->mNumVertices == 0 ||
            !mesh->mVertices) {
            continue;
        }
        if (mesh->mNumFaces > 0 && !mesh->mFaces) {
            continue;
        }

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        vertices.reserve(mesh->mNumVertices);
        if (static_cast<size_t>(mesh->mNumFaces) >
            (std::numeric_limits<size_t>::max)() / 3u) {
            continue;
        }
        indices.reserve(static_cast<size_t>(mesh->mNumFaces) * 3u);

        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex v{};

            v.position =
                SanitizeFloat3(mesh->mVertices[i], {0.0f, 0.0f, 0.0f});
            v.bindPosition = v.position;
            if (mesh->HasNormals()) {
                v.normal = SanitizeNormal(mesh->mNormals[i]);
            }

            if (mesh->HasTextureCoords(0)) {
                v.uv = {FiniteOr(mesh->mTextureCoords[0][i].x, 0.0f),
                        FiniteOr(mesh->mTextureCoords[0][i].y, 0.0f)};
            } else {
                v.uv = {0.0f, 0.0f};
            }

            if (mesh->HasTangentsAndBitangents()) {
                v.tangent = SanitizeTangent(mesh->mTangents[i]);
            }

            vertices.push_back(v);
        }

        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            const aiFace &face = mesh->mFaces[i];
            if (face.mNumIndices != 3 || !face.mIndices) {
                continue;
            }

            uint32_t triangle[3]{};
            bool faceValid = true;
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                if (face.mIndices[j] >= vertices.size()) {
                    faceValid = false;
                    break;
                }
                triangle[j] = face.mIndices[j];
            }
            if (!faceValid) {
                continue;
            }
            indices.push_back(triangle[0]);
            indices.push_back(triangle[1]);
            indices.push_back(triangle[2]);
        }

        if (vertices.empty() || indices.empty()) {
            continue;
        }

        ModelSubMesh subMesh{};
        if (vertices.size() >
                static_cast<size_t>((std::numeric_limits<uint32_t>::max)()) ||
            indices.size() >
                static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
            continue;
        }
        subMesh.vertexCount = static_cast<uint32_t>(vertices.size());
        subMesh.sourcePositions.reserve(vertices.size());
        subMesh.sourceBoundsMin = vertices.front().position;
        subMesh.sourceBoundsMax = vertices.front().position;
        for (const Vertex &vertex : vertices) {
            subMesh.sourcePositions.push_back(vertex.position);
            subMesh.sourceBoundsMin.x =
                (std::min)(subMesh.sourceBoundsMin.x, vertex.position.x);
            subMesh.sourceBoundsMin.y =
                (std::min)(subMesh.sourceBoundsMin.y, vertex.position.y);
            subMesh.sourceBoundsMin.z =
                (std::min)(subMesh.sourceBoundsMin.z, vertex.position.z);
            subMesh.sourceBoundsMax.x =
                (std::max)(subMesh.sourceBoundsMax.x, vertex.position.x);
            subMesh.sourceBoundsMax.y =
                (std::max)(subMesh.sourceBoundsMax.y, vertex.position.y);
            subMesh.sourceBoundsMax.z =
                (std::max)(subMesh.sourceBoundsMax.z, vertex.position.z);
        }

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
                    boneIndex =
                        CheckedUint32Size(model.bones.size(),
                                          "AssimpMeshLoader bone count overflow");
                    if (boneIndex == UINT32_MAX) {
                        continue;
                    }
                    if (boneIndex >
                        static_cast<uint32_t>((std::numeric_limits<int>::max)())) {
                        continue;
                    }

                    model.boneMap[boneName] = boneIndex;

                    BoneInfo info{};
                    info.name = boneName;

                    info.offsetMatrix = ToMatrix(bone->mOffsetMatrix);

                    model.bones.push_back(info);
                } else {
                    boneIndex = it->second;
                }

                JointWeightData &jointWeightData =
                    subMesh.skinClusterData[boneName];
                jointWeightData.inverseBindPoseMatrix =
                    model.bones[boneIndex].offsetMatrix;

                for (unsigned int w = 0; w < bone->mNumWeights; w++) {
                    uint32_t vertexId = bone->mWeights[w].mVertexId;
                    const float weight =
                        ClampFinite(bone->mWeights[w].mWeight, 0.0f, 1.0f,
                                    0.0f);

                    if (vertexId >= vertices.size() || weight <= 0.0f) {
                        continue;
                    }

                    jointWeightData.vertexWeights.push_back({weight, vertexId});
                }
            }
        }

        aiMaterial *mat = nullptr;
        uint32_t textureId = 0;
        uint32_t normalTextureId = UINT32_MAX;
        bool hasTexture = false;
        bool hasNormalTexture = false;

        if (scene->HasMaterials() &&
            mesh->mMaterialIndex < scene->mNumMaterials) {
            mat = scene->mMaterials[mesh->mMaterialIndex];

            auto tryLoadTexture = [&](aiTextureType textureType,
                                      uint32_t &outTextureId) -> bool {
                aiString texPath;
                if (!mat ||
                    mat->GetTexture(textureType, 0, &texPath) != AI_SUCCESS) {
                    return false;
                }

                std::string texName = texPath.C_Str();

                if (!texName.empty() && texName[0] == '*') {
                    unsigned int texIndex = 0;
                    if (!TryParseEmbeddedTextureIndex(texName, texIndex) ||
                        texIndex >= scene->mNumTextures) {
                        return false;
                    }

                    aiTexture *tex = scene->mTextures[texIndex];
                    if (!tex) {
                        return false;
                    }

                    if (tex->mHeight == 0) {
                        if (tex->mWidth == 0 || tex->pcData == nullptr) {
                            return false;
                        }
                        outTextureId = textureManager_->LoadFromMemory(
                            reinterpret_cast<const uint8_t *>(tex->pcData),
                            tex->mWidth);
                        return true;
                    }

                    if (tex->mWidth == 0 || tex->mHeight == 0 ||
                        tex->pcData == nullptr) {
                        return false;
                    }
                    if (static_cast<size_t>(tex->mWidth) >
                        (std::numeric_limits<size_t>::max)() /
                            static_cast<size_t>(tex->mHeight)) {
                        return false;
                    }
                    const size_t pixelCount =
                        static_cast<size_t>(tex->mWidth) *
                        static_cast<size_t>(tex->mHeight);
                    if (pixelCount >
                        (std::numeric_limits<size_t>::max)() / 4u) {
                        return false;
                    }

                    std::vector<uint8_t> pixels(pixelCount * 4u);
                    for (size_t pixelIndex = 0; pixelIndex < pixelCount;
                         ++pixelIndex) {
                        const aiTexel &src = tex->pcData[pixelIndex];
                        const size_t dst = pixelIndex * 4u;
                        pixels[dst + 0u] = src.r;
                        pixels[dst + 1u] = src.g;
                        pixels[dst + 2u] = src.b;
                        pixels[dst + 3u] = src.a;
                    }
                    outTextureId = textureManager_->CreateFromRgbaPixels(
                        tex->mWidth, tex->mHeight, pixels.data());
                    return true;
                }

                std::filesystem::path modelPath(path);
                auto fullPath = modelPath.parent_path() / texName;
                outTextureId = textureManager_->Load(fullPath.wstring());
                return true;
            };

            hasTexture = tryLoadTexture(aiTextureType_BASE_COLOR, textureId) ||
                         tryLoadTexture(aiTextureType_DIFFUSE, textureId);
            hasNormalTexture =
                tryLoadTexture(aiTextureType_NORMALS, normalTextureId) ||
                tryLoadTexture(aiTextureType_HEIGHT, normalTextureId);
        }

        uint32_t meshId = meshManager_->CreateMesh(
            vertices.data(), sizeof(Vertex), subMesh.vertexCount,
            indices.data(), static_cast<uint32_t>(indices.size()));
        if (meshId == UINT32_MAX) {
            continue;
        }

        Material material{};
        material.color = {1, 1, 1, 1};
        material.reflectionStrength = 0.18f;
        material.reflectionFresnelStrength = 0.12f;

        aiColor4D diffuse;

        if (mat && aiGetMaterialColor(mat, AI_MATKEY_COLOR_DIFFUSE, &diffuse) ==
                       AI_SUCCESS) {
            material.color.x = ClampFinite(diffuse.r, 0.0f, 1.0f, 1.0f);
            material.color.y = ClampFinite(diffuse.g, 0.0f, 1.0f, 1.0f);
            material.color.z = ClampFinite(diffuse.b, 0.0f, 1.0f, 1.0f);
        }

        float opacity = 1.0f;

        if (mat && mat->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
            material.color.w = ClampFinite(opacity, 0.0f, 1.0f, 1.0f);
        }

        XMStoreFloat4x4(&material.uvTransform,
                        XMMatrixTranspose(XMMatrixIdentity()));

        material.enableTexture = hasTexture ? 1 : 0;
        material.enableNormalMap = hasNormalTexture ? 1 : 0;
        material.baseColorTextureId = hasTexture ? textureId : UINT32_MAX;
        material.normalTextureId =
            hasNormalTexture ? normalTextureId : UINT32_MAX;

        subMesh.meshId = meshId;
        subMesh.textureId = textureId;
        subMesh.normalTextureId = normalTextureId;
        subMesh.materialId = materialManager_->CreateMaterial(material);

        model.subMeshes.push_back(subMesh);
    }

    if (model.subMeshes.empty()) {
        return;
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

void AssimpMeshLoader::BuildBoneHierarchy(const aiScene *scene,
                                          Model &model) const {
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

    ReorderBonesParentFirst(model);
}

void AssimpMeshLoader::ReorderBonesParentFirst(Model &model) const {
    const size_t boneCount = model.bones.size();
    if (boneCount <= 1) {
        return;
    }
    if (boneCount > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return;
    }

    std::vector<std::vector<size_t>> children(boneCount);
    std::vector<size_t> roots;
    roots.reserve(boneCount);

    for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        const int parentIndex = model.bones[boneIndex].parentIndex;
        if (parentIndex >= 0 && static_cast<size_t>(parentIndex) < boneCount) {
            children[static_cast<size_t>(parentIndex)].push_back(boneIndex);
        } else {
            roots.push_back(boneIndex);
        }
    }

    std::vector<BoneInfo> orderedBones;
    std::vector<int> oldToNew(boneCount, -1);
    orderedBones.reserve(boneCount);

    std::function<void(size_t, int)> visit = [&](size_t oldIndex,
                                                 int newParentIndex) {
        if (oldToNew[oldIndex] >= 0) {
            return;
        }

        BoneInfo bone = model.bones[oldIndex];
        bone.parentIndex = newParentIndex;
        const int newIndex =
            CheckedIntSize(orderedBones.size(),
                           "AssimpMeshLoader reordered bone count overflow");
        oldToNew[oldIndex] = newIndex;
        orderedBones.push_back(bone);

        for (size_t childIndex : children[oldIndex]) {
            visit(childIndex, newIndex);
        }
    };

    for (size_t rootIndex : roots) {
        visit(rootIndex, -1);
    }

    for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        if (oldToNew[boneIndex] < 0) {
            visit(boneIndex, -1);
        }
    }

    model.bones = std::move(orderedBones);
    model.boneMap.clear();
    for (size_t boneIndex = 0; boneIndex < model.bones.size(); ++boneIndex) {
        model.boneMap[model.bones[boneIndex].name] =
            CheckedUint32Size(boneIndex,
                              "AssimpMeshLoader reordered bone count overflow");
    }
}

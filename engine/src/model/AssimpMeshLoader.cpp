#include "model/AssimpMeshLoader.h"
#include "core/Numeric.h"
#include "model/Material.h"
#include "model/MaterialManager.h"
#include "model/MeshManager.h"
#include "model/ModelLimits.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include "texture/TextureLimits.h"
#include <DirectXMath.h>
#include <algorithm>
#include <assimp/GltfMaterial.h>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace DirectX;

namespace {
using Numeric::ClampFinite;
using Numeric::FiniteOr;

constexpr float kEpsilon = 0.000001f;
constexpr size_t kMaxAssimpNodeTraversal = 65536u;

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

bool BuildNodeMap(const aiNode *root,
                  std::unordered_map<std::string, const aiNode *> &nodes) {
    if (!root) {
        return false;
    }

    std::vector<const aiNode *> stack;
    try {
        stack.reserve(256u);
        nodes.reserve(256u);
        stack.push_back(root);
    } catch (...) {
        return false;
    }

    size_t visited = 0;
    while (!stack.empty()) {
        const aiNode *node = stack.back();
        stack.pop_back();
        if (!node) {
            continue;
        }
        if (++visited > kMaxAssimpNodeTraversal) {
            return false;
        }

        try {
            nodes.emplace(node->mName.C_Str(), node);
        } catch (...) {
            return false;
        }

        if (node->mNumChildren > 0 && node->mChildren == nullptr) {
            return false;
        }
        for (unsigned int i = node->mNumChildren; i > 0; --i) {
            try {
                stack.push_back(node->mChildren[i - 1u]);
            } catch (...) {
                return false;
            }
        }
    }

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

    if (scene->mNumMeshes > ModelLimits::kMaxMeshes ||
        scene->mNumMaterials > ModelLimits::kMaxMaterials ||
        scene->mNumTextures > ModelLimits::kMaxEmbeddedTextures) {
        return;
    }
    if ((scene->mNumMeshes > 0 && scene->mMeshes == nullptr) ||
        (scene->mNumMaterials > 0 && scene->mMaterials == nullptr) ||
        (scene->mNumTextures > 0 && scene->mTextures == nullptr)) {
        return;
    }
    try {
        model.subMeshes.reserve(scene->mNumMeshes);
    } catch (...) {
        return;
    }

    std::size_t loadedVertices = 0;
    std::size_t loadedFaces = 0;
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
        if (mesh->mNumBones > 0 && !mesh->mBones) {
            continue;
        }
        if (mesh->mNumVertices > ModelLimits::kMaxVerticesPerMesh ||
            mesh->mNumFaces > ModelLimits::kMaxFacesPerMesh ||
            mesh->mNumBones > ModelLimits::kMaxBonesPerMesh ||
            mesh->mNumVertices > ModelLimits::kMaxTotalVertices - loadedVertices ||
            mesh->mNumFaces > ModelLimits::kMaxTotalFaces - loadedFaces) {
            continue;
        }

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        if (static_cast<size_t>(mesh->mNumFaces) >
            (std::numeric_limits<size_t>::max)() / 3u) {
            continue;
        }
        try {
            vertices.reserve(mesh->mNumVertices);
            indices.reserve(static_cast<size_t>(mesh->mNumFaces) * 3u);
        } catch (...) {
            continue;
        }

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
        try {
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
        } catch (...) {
            continue;
        }

        const size_t boneRollbackSize = model.bones.size();
        std::vector<std::string> addedBoneNames;
        auto rollbackAddedBones = [&]() {
            for (const std::string &name : addedBoneNames) {
                model.boneMap.erase(name);
            }
            model.bones.resize(boneRollbackSize);
        };

        if (mesh->HasBones()) {
            try {
                addedBoneNames.reserve(mesh->mNumBones);
                for (unsigned int i = 0; i < mesh->mNumBones; i++) {
                    aiBone *bone = mesh->mBones[i];

                    if (!bone) {
                        continue;
                    }
                    if (bone->mNumWeights > 0 && !bone->mWeights) {
                        continue;
                    }

                    std::string boneName = bone->mName.C_Str();
                    uint32_t boneIndex = 0;

                    auto it = model.boneMap.find(boneName);

                    if (it == model.boneMap.end()) {
                        boneIndex = CheckedUint32Size(
                            model.bones.size(),
                            "AssimpMeshLoader bone count overflow");
                        if (boneIndex == UINT32_MAX) {
                            continue;
                        }
                        if (boneIndex > static_cast<uint32_t>(
                                            (std::numeric_limits<int>::max)())) {
                            continue;
                        }

                        BoneInfo info{};
                        info.name = boneName;
                        info.offsetMatrix = ToMatrix(bone->mOffsetMatrix);

                        addedBoneNames.push_back(boneName);
                        model.bones.push_back(info);
                        model.boneMap.emplace(boneName, boneIndex);
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

                        jointWeightData.vertexWeights.push_back(
                            {weight, vertexId});
                    }
                }
            } catch (...) {
                rollbackAddedBones();
                continue;
            }
        }

        aiMaterial *mat = nullptr;
        uint32_t textureId = textureManager_->GetWhiteTextureId();
        uint32_t normalTextureId = UINT32_MAX;
        bool hasTexture = false;
        bool hasNormalTexture = false;

        try {
            if (scene->HasMaterials() &&
                mesh->mMaterialIndex < scene->mNumMaterials) {
                mat = scene->mMaterials[mesh->mMaterialIndex];

                auto isLoadedTexture = [this](uint32_t textureId) {
                    return textureManager_->IsValidTextureId(textureId) &&
                           textureId != textureManager_->GetWhiteTextureId();
                };

                auto tryLoadTexture = [&](aiTextureType textureType,
                                          uint32_t &outTextureId) -> bool {
                    aiString texPath;
                    if (!mat || mat->GetTexture(textureType, 0, &texPath) !=
                                    AI_SUCCESS) {
                        return false;
                    }

                    std::string texName = texPath.C_Str();

                    if (!texName.empty() && texName[0] == '*') {
                        unsigned int texIndex = 0;
                        if (!TryParseEmbeddedTextureIndex(texName, texIndex) ||
                            texIndex >= scene->mNumTextures) {
                            return false;
                        }
                        if (scene->mTextures == nullptr) {
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
                            if (tex->mWidth >
                                ModelLimits::kMaxEmbeddedTextureBytes) {
                                return false;
                            }
                            outTextureId = textureManager_->LoadFromMemory(
                                reinterpret_cast<const uint8_t *>(tex->pcData),
                                tex->mWidth);
                            return isLoadedTexture(outTextureId);
                        }

                        if (tex->mWidth == 0 || tex->mHeight == 0 ||
                            tex->pcData == nullptr) {
                            return false;
                        }
                        if (tex->mWidth > TextureLimits::kMaxDimension ||
                            tex->mHeight > TextureLimits::kMaxDimension) {
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
                                ModelLimits::kMaxEmbeddedTexturePixels ||
                            pixelCount >
                                (std::numeric_limits<size_t>::max)() / 4u) {
                            return false;
                        }
                        if (pixelCount >
                            ModelLimits::kMaxEmbeddedTextureBytes / 4u) {
                            return false;
                        }

                        std::vector<uint8_t> pixels;
                        try {
                            pixels.resize(pixelCount * 4u);
                        } catch (...) {
                            return false;
                        }
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
                        return isLoadedTexture(outTextureId);
                    }

                    std::filesystem::path modelPath(path);
                    auto fullPath = modelPath.parent_path() / texName;
                    outTextureId = textureManager_->Load(fullPath.wstring());
                    return isLoadedTexture(outTextureId);
                };

                hasTexture =
                    tryLoadTexture(aiTextureType_BASE_COLOR, textureId) ||
                    tryLoadTexture(aiTextureType_DIFFUSE, textureId);
                hasNormalTexture =
                    tryLoadTexture(aiTextureType_NORMALS, normalTextureId) ||
                    tryLoadTexture(aiTextureType_HEIGHT, normalTextureId);
            }
        } catch (...) {
            rollbackAddedBones();
            continue;
        }

        uint32_t meshId = meshManager_->CreateMesh(
            vertices.data(), sizeof(Vertex), subMesh.vertexCount,
            indices.data(), static_cast<uint32_t>(indices.size()));
        if (meshId == UINT32_MAX) {
            rollbackAddedBones();
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
        try {
            subMesh.materialId = materialManager_->CreateMaterial(material);
        } catch (...) {
            rollbackAddedBones();
            meshManager_->DestroyMesh(meshId);
            continue;
        }
        if (subMesh.materialId == UINT32_MAX) {
            rollbackAddedBones();
            meshManager_->DestroyMesh(meshId);
            continue;
        }

        try {
            model.subMeshes.push_back(std::move(subMesh));
        } catch (...) {
            rollbackAddedBones();
            materialManager_->DestroyMaterial(subMesh.materialId);
            meshManager_->DestroyMesh(meshId);
            continue;
        }
        loadedVertices += mesh->mNumVertices;
        loadedFaces += mesh->mNumFaces;
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

    std::vector<const aiNode *> stack;
    try {
        stack.reserve(256u);
        stack.push_back(node);
    } catch (...) {
        return nullptr;
    }

    size_t visited = 0;
    while (!stack.empty()) {
        const aiNode *current = stack.back();
        stack.pop_back();
        if (!current) {
            continue;
        }
        if (++visited > kMaxAssimpNodeTraversal) {
            return nullptr;
        }
        if (name == current->mName.C_Str()) {
            return current;
        }

        if (current->mNumChildren > 0 && current->mChildren == nullptr) {
            return nullptr;
        }
        for (unsigned int i = current->mNumChildren; i > 0; --i) {
            try {
                stack.push_back(current->mChildren[i - 1u]);
            } catch (...) {
                return nullptr;
            }
        }
    }

    return nullptr;
}

void AssimpMeshLoader::BuildBoneHierarchy(const aiScene *scene,
                                          Model &model) const {
    if (!scene || !scene->mRootNode) {
        return;
    }

    std::unordered_map<std::string, const aiNode *> nodes;
    if (!BuildNodeMap(scene->mRootNode, nodes)) {
        return;
    }

    for (size_t i = 0; i < model.bones.size(); i++) {
        const std::string &boneName = model.bones[i].name;

        const auto nodeIt = nodes.find(boneName);
        const aiNode *node = nodeIt != nodes.end() ? nodeIt->second : nullptr;
        if (!node) {
            model.bones[i].parentIndex = -1;
            model.bones[i].localBindMatrix = ToMatrix(aiMatrix4x4());
            model.bones[i].parentAdjustmentMatrix = ToMatrix(aiMatrix4x4());
            continue;
        }

        aiMatrix4x4 adjustment{};
        int parentIndex = -1;
        const aiNode *parent = node->mParent;
        size_t parentDepth = 0;

        while (parent) {
            if (++parentDepth > kMaxAssimpNodeTraversal) {
                parentIndex = -1;
                break;
            }
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

    std::vector<std::vector<size_t>> children;
    std::vector<size_t> roots;
    try {
        children.resize(boneCount);
        roots.reserve(boneCount);
    } catch (...) {
        return;
    }

    for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        const int parentIndex = model.bones[boneIndex].parentIndex;
        try {
            if (parentIndex >= 0 &&
                static_cast<size_t>(parentIndex) < boneCount) {
                children[static_cast<size_t>(parentIndex)].push_back(boneIndex);
            } else {
                roots.push_back(boneIndex);
            }
        } catch (...) {
            return;
        }
    }

    std::vector<BoneInfo> orderedBones;
    std::vector<int> oldToNew;
    try {
        oldToNew.assign(boneCount, -1);
        orderedBones.reserve(boneCount);
    } catch (...) {
        return;
    }

    auto visit = [&](size_t rootIndex, int rootParentIndex) {
        std::vector<std::pair<size_t, int>> stack;
        try {
            stack.reserve(64u);
            stack.push_back({rootIndex, rootParentIndex});
        } catch (...) {
            return false;
        }

        while (!stack.empty()) {
            const auto entry = stack.back();
            stack.pop_back();
            const size_t oldIndex = entry.first;
            const int newParentIndex = entry.second;

            if (oldIndex >= boneCount || oldToNew[oldIndex] >= 0) {
                continue;
            }

            const int newIndex = CheckedIntSize(
                orderedBones.size(),
                "AssimpMeshLoader reordered bone count overflow");
            try {
                BoneInfo bone = model.bones[oldIndex];
                bone.parentIndex = newParentIndex;
                orderedBones.push_back(std::move(bone));
            } catch (...) {
                return false;
            }
            oldToNew[oldIndex] = newIndex;

            const std::vector<size_t> &childList = children[oldIndex];
            for (size_t i = childList.size(); i > 0; --i) {
                try {
                    stack.push_back({childList[i - 1u], newIndex});
                } catch (...) {
                    return false;
                }
            }
        }
        return true;
    };

    for (size_t rootIndex : roots) {
        if (!visit(rootIndex, -1)) {
            return;
        }
    }

    for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        if (oldToNew[boneIndex] < 0) {
            if (!visit(boneIndex, -1)) {
                return;
            }
        }
    }

    std::unordered_map<std::string, uint32_t> reorderedBoneMap;
    try {
        reorderedBoneMap.reserve(orderedBones.size());
        for (size_t boneIndex = 0; boneIndex < orderedBones.size();
             ++boneIndex) {
            reorderedBoneMap.emplace(
                orderedBones[boneIndex].name,
                CheckedUint32Size(
                    boneIndex,
                    "AssimpMeshLoader reordered bone count overflow"));
        }
    } catch (...) {
        return;
    }

    model.bones = std::move(orderedBones);
    model.boneMap = std::move(reorderedBoneMap);
}

#include "model/AssimpMeshLoader.h"

#include "AssimpMeshLoaderMaterial.h"
#include "AssimpMeshLoaderUtils.h"
#include "core/Numeric.h"
#include "core/ResourceHandle.h"
#include "model/Material.h"
#include "model/MaterialManager.h"
#include "model/MeshManager.h"
#include "model/ModelLimits.h"
#include "model/Vertex.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

using namespace DirectX;

namespace {
using AssimpMeshLoaderUtils::CheckedUint32Size;
using AssimpMeshLoaderUtils::ToMatrix;
using Numeric::ClampFinite;
using Numeric::FiniteOr;

constexpr float kEpsilon = 0.000001f;

XMFLOAT3 SanitizeFloat3(const aiVector3D& value, const XMFLOAT3& fallback) {
    return {FiniteOr(value.x, fallback.x), FiniteOr(value.y, fallback.y),
            FiniteOr(value.z, fallback.z)};
}

XMFLOAT3 SanitizeNormal(const aiVector3D& value) {
    XMFLOAT3 normal = SanitizeFloat3(value, {0.0f, 1.0f, 0.0f});
    XMVECTOR vector = XMLoadFloat3(&normal);
    const float lengthSq = XMVectorGetX(XMVector3LengthSq(vector));
    if (!std::isfinite(lengthSq) || lengthSq <= kEpsilon) {
        return {0.0f, 1.0f, 0.0f};
    }
    XMStoreFloat3(&normal, XMVector3Normalize(vector));
    return normal;
}

XMFLOAT4 SanitizeTangent(const aiVector3D& value) {
    XMFLOAT3 tangent = SanitizeFloat3(value, {1.0f, 0.0f, 0.0f});
    XMVECTOR vector = XMLoadFloat3(&tangent);
    const float lengthSq = XMVectorGetX(XMVector3LengthSq(vector));
    if (!std::isfinite(lengthSq) || lengthSq <= kEpsilon) {
        return {1.0f, 0.0f, 0.0f, 1.0f};
    }
    XMStoreFloat3(&tangent, XMVector3Normalize(vector));
    return {tangent.x, tangent.y, tangent.z, 1.0f};
}

bool IsAssimpMeshReadable(const aiMesh& mesh) {
    if (!mesh.HasPositions() || mesh.mNumVertices == 0 || !mesh.mVertices) {
        return false;
    }
    if (mesh.mNumFaces > 0 && !mesh.mFaces) {
        return false;
    }
    if (mesh.mNumBones > 0 && !mesh.mBones) {
        return false;
    }
    return true;
}

bool IsAssimpMeshWithinBudget(const aiMesh& mesh, size_t loadedVertices, size_t loadedFaces) {
    return loadedVertices <= ModelLimits::kMaxTotalVertices &&
           loadedFaces <= ModelLimits::kMaxTotalFaces &&
           mesh.mNumVertices <= ModelLimits::kMaxVerticesPerMesh &&
           mesh.mNumFaces <= ModelLimits::kMaxFacesPerMesh &&
           mesh.mNumBones <= ModelLimits::kMaxBonesPerMesh &&
           mesh.mNumVertices <= ModelLimits::kMaxTotalVertices - loadedVertices &&
           mesh.mNumFaces <= ModelLimits::kMaxTotalFaces - loadedFaces;
}

bool BuildAssimpMeshGeometry(const aiMesh& mesh, std::vector<Vertex>& vertices,
                             std::vector<uint32_t>& indices) {
    if (static_cast<size_t>(mesh.mNumFaces) > (std::numeric_limits<size_t>::max)() / 3u) {
        return false;
    }

    vertices.reserve(mesh.mNumVertices);
    indices.reserve(static_cast<size_t>(mesh.mNumFaces) * 3u);
    for (unsigned int i = 0; i < mesh.mNumVertices; i++) {
        Vertex v{};
        v.position = SanitizeFloat3(mesh.mVertices[i], {0.0f, 0.0f, 0.0f});
        v.customPosition0 = v.position;

        if (mesh.HasNormals()) {
            v.normal = SanitizeNormal(mesh.mNormals[i]);
        }

        if (mesh.HasTextureCoords(0)) {
            v.uv = {FiniteOr(mesh.mTextureCoords[0][i].x, 0.0f),
                    FiniteOr(mesh.mTextureCoords[0][i].y, 0.0f)};
        } else {
            v.uv = {0.0f, 0.0f};
        }

        if (mesh.HasTangentsAndBitangents()) {
            v.tangent = SanitizeTangent(mesh.mTangents[i]);
        }

        vertices.push_back(v);
    }

    for (unsigned int i = 0; i < mesh.mNumFaces; i++) {
        const aiFace& face = mesh.mFaces[i];
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

    return !vertices.empty() && !indices.empty();
}

bool PopulateSubMeshSourceData(ModelSubMesh& subMesh, const std::vector<Vertex>& vertices) {
    if (vertices.empty() ||
        vertices.size() > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return false;
    }

    subMesh.vertexCount = static_cast<uint32_t>(vertices.size());
    subMesh.sourcePositions.reserve(vertices.size());
    subMesh.sourceBoundsMin = vertices.front().position;
    subMesh.sourceBoundsMax = vertices.front().position;
    for (const Vertex& vertex : vertices) {
        subMesh.sourcePositions.push_back(vertex.position);
        subMesh.sourceBoundsMin.x = (std::min)(subMesh.sourceBoundsMin.x, vertex.position.x);
        subMesh.sourceBoundsMin.y = (std::min)(subMesh.sourceBoundsMin.y, vertex.position.y);
        subMesh.sourceBoundsMin.z = (std::min)(subMesh.sourceBoundsMin.z, vertex.position.z);
        subMesh.sourceBoundsMax.x = (std::max)(subMesh.sourceBoundsMax.x, vertex.position.x);
        subMesh.sourceBoundsMax.y = (std::max)(subMesh.sourceBoundsMax.y, vertex.position.y);
        subMesh.sourceBoundsMax.z = (std::max)(subMesh.sourceBoundsMax.z, vertex.position.z);
    }
    return true;
}

void RollbackAddedBones(Model& model, size_t boneRollbackSize,
                        const std::vector<std::string>& addedBoneNames) {
    for (const std::string& name : addedBoneNames) {
        model.boneMap.erase(name);
    }
    model.bones.resize(boneRollbackSize);
}

void AppendMeshBoneWeights(const aiMesh& mesh, size_t vertexCount, Model& model,
                           ModelSubMesh& subMesh,
                           std::vector<std::string>& addedBoneNames) {
    if (!mesh.HasBones()) {
        return;
    }

    addedBoneNames.reserve(mesh.mNumBones);
    for (unsigned int i = 0; i < mesh.mNumBones; i++) {
        aiBone* bone = mesh.mBones[i];
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
            boneIndex =
                CheckedUint32Size(model.bones.size(), "AssimpMeshLoader bone count overflow");
            if (!IsValidResourceId(boneIndex)) {
                continue;
            }
            if (boneIndex > static_cast<uint32_t>((std::numeric_limits<int>::max)())) {
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

        JointWeightData& jointWeightData = subMesh.skinClusterData[boneName];
        jointWeightData.inverseBindPoseMatrix = model.bones[boneIndex].offsetMatrix;

        for (unsigned int w = 0; w < bone->mNumWeights; w++) {
            const uint32_t vertexId = bone->mWeights[w].mVertexId;
            const float weight = ClampFinite(bone->mWeights[w].mWeight, 0.0f, 1.0f, 0.0f);

            if (vertexId >= vertexCount || weight <= 0.0f) {
                continue;
            }

            jointWeightData.vertexWeights.push_back({weight, vertexId});
        }
    }
}

} // namespace

void AssimpMeshLoader::Initialize(TextureManager* textureManager, MeshManager* meshManager,
                                  MaterialManager* materialManager) {
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

void AssimpMeshLoader::LoadMeshes(const aiScene* scene, const std::string& path,
                                  Model& model) const {
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
    model.subMeshes.reserve(scene->mNumMeshes);
    std::size_t loadedVertices = 0;
    std::size_t loadedFaces = 0;
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; meshIndex++) {
        aiMesh* mesh = scene->mMeshes[meshIndex];
        if (!mesh) {
            continue;
        }
        if (!IsAssimpMeshReadable(*mesh) ||
            !IsAssimpMeshWithinBudget(*mesh, loadedVertices, loadedFaces)) {
            continue;
        }

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        ModelSubMesh subMesh{};

        if (!BuildAssimpMeshGeometry(*mesh, vertices, indices) ||
            indices.size() > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
            continue;
        }
        if (!PopulateSubMeshSourceData(subMesh, vertices)) {
            continue;
        }

        const size_t boneRollbackSize = model.bones.size();
        std::vector<std::string> addedBoneNames;
        AppendMeshBoneWeights(*mesh, vertices.size(), model, subMesh,
                              addedBoneNames);

        AssimpMeshLoaderMaterial::MaterialSource materialSource{};
        materialSource =
            AssimpMeshLoaderMaterial::LoadSource(*textureManager_, *scene, *mesh, path);
        uint32_t meshId =
            meshManager_->CreateMesh(vertices.data(), sizeof(Vertex), subMesh.vertexCount,
                                     indices.data(), static_cast<uint32_t>(indices.size()));
        if (!IsValidResourceId(meshId)) {
            RollbackAddedBones(model, boneRollbackSize, addedBoneNames);
            continue;
        }

        Material material = AssimpMeshLoaderMaterial::BuildMaterial(materialSource);

        subMesh.meshId = meshId;
        subMesh.textureId = materialSource.baseColorTextureId;
        subMesh.normalTextureId = materialSource.normalTextureId;
        subMesh.materialId = materialManager_->CreateMaterial(material);
        if (!IsValidResourceId(subMesh.materialId)) {
            RollbackAddedBones(model, boneRollbackSize, addedBoneNames);
            meshManager_->DestroyMesh(meshId);
            continue;
        }

        model.subMeshes.push_back(std::move(subMesh));
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

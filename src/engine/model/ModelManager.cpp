#include "ModelManager.h"
#include "DirectXCommon.h"
#include "MaterialManager.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "Vertex.h"
#include <DirectXMath.h>
#include <array>
#include <filesystem>

using namespace DirectX;

namespace {

constexpr std::array<Vertex, 4> kPlaneVertices = {{
    {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
}};

constexpr std::array<uint32_t, 6> kPlaneIndices = {0, 1, 2, 2, 1, 3};

} // namespace

void ModelManager::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                              TextureManager *textureManager) {
    dxCommon_ = dxCommon;
    textureManager_ = textureManager;

    meshManager_.Initialize(dxCommon_);
    materialManager_.Initialize(dxCommon_);

    assimpLoader_.Initialize(textureManager_, &meshManager_, &materialManager_);

    modelRenderer_.Initialize(dxCommon_, srvManager, &meshManager_,
                              textureManager_, &materialManager_);
}

uint32_t ModelManager::Load(const std::wstring &path) {
    std::filesystem::path p = path;
    std::string pathStr = p.string();

    Model model = assimpLoader_.Load(pathStr);
    modelRenderer_.CreateSkinClusters(model);

    if (!model.animations.empty()) {
        model.currentAnimation = model.animations.begin()->first;
        model.animationTime = 0.0f;
        model.isLoop = true;
        model.isPlaying = true;
        model.animationFinished = false;
    }

    animator_.Update(model, 0.0f);
    modelRenderer_.UpdateSkinClusters(model);

    models_.push_back(model);
    uint32_t modelId = static_cast<uint32_t>(models_.size() - 1);

    return modelId;
}

uint32_t ModelManager::CreatePlane(uint32_t textureId, const Material &material) {
    Material planeMaterial = material;
    XMStoreFloat4x4(&planeMaterial.uvTransform,
                    XMMatrixTranspose(XMMatrixIdentity()));

    Model model{};
    ModelSubMesh subMesh{};
    subMesh.vertexCount = static_cast<uint32_t>(kPlaneVertices.size());
    subMesh.meshId = meshManager_.CreateMesh(
        kPlaneVertices.data(), sizeof(Vertex),
        static_cast<uint32_t>(kPlaneVertices.size()), kPlaneIndices.data(),
        static_cast<uint32_t>(kPlaneIndices.size()));
    subMesh.textureId = textureId;
    subMesh.materialId = materialManager_.CreateMaterial(planeMaterial);

    model.subMeshes.push_back(subMesh);
    model.meshId = subMesh.meshId;
    model.textureId = textureId;
    model.materialId = subMesh.materialId;

    modelRenderer_.CreateSkinClusters(model);

    models_.push_back(model);
    return static_cast<uint32_t>(models_.size() - 1);
}

void ModelManager::UpdateAnimation(uint32_t modelId, float deltaTime) {
    if (modelId >= models_.size()) {
        return;
    }

    animator_.Update(models_[modelId], deltaTime);
    modelRenderer_.UpdateSkinClusters(models_[modelId]);
}

void ModelManager::PlayAnimation(uint32_t modelId,
                                 const std::string &animationName, bool loop) {
    if (modelId >= models_.size()) {
        return;
    }

    animator_.Play(models_[modelId], animationName, loop);
}

bool ModelManager::IsAnimationFinished(uint32_t modelId) const {
    if (modelId >= models_.size()) {
        return false;
    }

    return animator_.IsFinished(models_[modelId]);
}

Model *ModelManager::GetModel(uint32_t modelId) {
    if (modelId >= models_.size()) {
        return nullptr;
    }

    return &models_[modelId];
}

const Model *ModelManager::GetModel(uint32_t modelId) const {
    if (modelId >= models_.size()) {
        return nullptr;
    }

    return &models_[modelId];
}

const Material &ModelManager::GetMaterial(uint32_t materialId) const {
    return materialManager_.GetMaterial(materialId);
}

void ModelManager::SetMaterial(uint32_t materialId, const Material &material) {
    materialManager_.SetMaterial(materialId, material);
}

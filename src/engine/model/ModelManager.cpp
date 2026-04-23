#include "ModelManager.h"
#include "DirectXCommon.h"
#include "MaterialManager.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "Vertex.h"
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <vector>

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

uint32_t ModelManager::CreateRing(uint32_t textureId, const Material &material,
                                  uint32_t divide, float outerRadius,
                                  float innerRadius) {
    if (divide < 3) {
        divide = 3;
    }

    outerRadius = (std::max)(outerRadius, 0.001f);
    innerRadius = (std::clamp)(innerRadius, 0.0f, outerRadius - 0.0001f);

    Material ringMaterial = material;
    XMStoreFloat4x4(&ringMaterial.uvTransform, XMMatrixTranspose(XMMatrixIdentity()));

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(static_cast<size_t>(divide) * 4u);
    indices.reserve(static_cast<size_t>(divide) * 6u);

    const float radianPerDivide =
        std::numbers::pi_v<float> * 2.0f / static_cast<float>(divide);

    for (uint32_t index = 0; index < divide; ++index) {
        const uint32_t base = static_cast<uint32_t>(vertices.size());

        const float angle = static_cast<float>(index) * radianPerDivide;
        const float angleNext = static_cast<float>(index + 1) * radianPerDivide;

        const float sinV = std::sin(angle);
        const float cosV = std::cos(angle);
        const float sinNext = std::sin(angleNext);
        const float cosNext = std::cos(angleNext);

        const float u = static_cast<float>(index) / static_cast<float>(divide);
        const float uNext =
            static_cast<float>(index + 1) / static_cast<float>(divide);

        vertices.push_back(
            {{-sinV * outerRadius, cosV * outerRadius, 0.0f},
             {0.0f, 0.0f, 1.0f},
             {u, 0.0f}});
        vertices.push_back(
            {{-sinNext * outerRadius, cosNext * outerRadius, 0.0f},
             {0.0f, 0.0f, 1.0f},
             {uNext, 0.0f}});
        vertices.push_back(
            {{-sinV * innerRadius, cosV * innerRadius, 0.0f},
             {0.0f, 0.0f, 1.0f},
             {u, 1.0f}});
        vertices.push_back(
            {{-sinNext * innerRadius, cosNext * innerRadius, 0.0f},
             {0.0f, 0.0f, 1.0f},
             {uNext, 1.0f}});

        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
        indices.push_back(base + 1);
    }

    Model model{};
    ModelSubMesh subMesh{};
    subMesh.vertexCount = static_cast<uint32_t>(vertices.size());
    subMesh.meshId = meshManager_.CreateMesh(
        vertices.data(), sizeof(Vertex), static_cast<uint32_t>(vertices.size()),
        indices.data(), static_cast<uint32_t>(indices.size()));
    subMesh.textureId = textureId;
    subMesh.materialId = materialManager_.CreateMaterial(ringMaterial);

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

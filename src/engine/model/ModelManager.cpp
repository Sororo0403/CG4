#include "ModelManager.h"
#include "DirectXCommon.h"
#include "MaterialManager.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include <filesystem>

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

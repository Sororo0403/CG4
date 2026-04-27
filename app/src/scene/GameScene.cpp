#include "AssetPaths.h"
#include "GameScene.h"
#include "DirectXCommon.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <algorithm>
#include <filesystem>

using namespace DirectX;

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    const float aspect = static_cast<float>(ctx.winApp->GetWidth()) /
                         static_cast<float>(ctx.winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.SetMode(CameraMode::LookAt);
    camera_.SetPosition({0.0f, 1.05f, -4.0f});
    camera_.LookAt({0.0f, 0.7f, 0.0f});
    camera_.UpdateMatrices();

    ctx.dxCommon->BeginUpload();
    InitializeParticleEffect();
    ctx.dxCommon->EndUpload();
    ctx.texture->ReleaseUploadBuffers();
}

void GameScene::Update() {
    const float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                         static_cast<float>((std::max)(ctx_->winApp->GetHeight(), 1));
    camera_.SetAspect(aspect);
    camera_.LookAt({0.0f, 0.7f, 0.0f});
    camera_.UpdateMatrices();

    if (hasGpuParticleSystem_) {
        gpuParticleSystem_.Update(ctx_->deltaTime);
    }
}

void GameScene::Draw() {
    if (!ctx_) {
        return;
    }

    if (hasGpuParticleSystem_) {
        gpuParticleSystem_.Draw(camera_);
    }
}

void GameScene::InitializeParticleEffect() {
    if (!ctx_ || !ctx_->texture) {
        return;
    }

    const std::filesystem::path texturePath = AssetPaths::kHitEffectTexture;
    if (!std::filesystem::exists(texturePath)) {
        return;
    }

    const uint32_t textureId = ctx_->texture->Load(texturePath.wstring());
    gpuParticleSystem_.Initialize(ctx_->dxCommon, ctx_->srv, ctx_->texture,
                                  textureId, 2048);
    gpuParticleSystem_.SetEmitterPosition({0.0f, 0.0f, 0.0f});
    gpuParticleSystem_.SetEmitterRadius(0.95f);
    gpuParticleSystem_.SetEmission(38, 0.10f);
    hasGpuParticleSystem_ = true;
}

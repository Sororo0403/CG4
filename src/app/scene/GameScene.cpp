#include "AssetPaths.h"
#include "GameScene.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "ModelManager.h"
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
    camera_.SetMode(CameraMode::Free);
    camera_.SetPosition({0.0f, 1.2f, -4.5f});
    camera_.SetRotation({0.0f, 0.0f, 0.0f});
    camera_.UpdateMatrices();

    ctx.dxCommon->BeginUpload();
    InitializeHitEffect();
    ctx.dxCommon->EndUpload();
    ctx.texture->ReleaseUploadBuffers();

    if (hasHitEffectModel_) {
        hitEmitter_.EmitNow();
    }
}

void GameScene::Update() {
    const float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                         static_cast<float>((std::max)(ctx_->winApp->GetHeight(), 1));
    camera_.SetAspect(aspect);
    camera_.Update(*ctx_->input, ctx_->deltaTime);
    camera_.UpdateMatrices();

    if (ctx_->input->IsKeyTrigger(DIK_SPACE) || ctx_->input->IsMouseTrigger(0)) {
        hitEmitter_.EmitNow();
    }

    hitEmitter_.Update(ctx_->deltaTime);
    hitParticleSystem_.Update(ctx_->deltaTime);
    if (hasGpuParticleSystem_) {
        gpuParticleSystem_.Update(ctx_->deltaTime);
    }
}

void GameScene::Draw() {
    if (!ctx_ || !ctx_->modelRenderer) {
        return;
    }

    if (hasGpuParticleSystem_) {
        gpuParticleSystem_.Draw(camera_);
    }
}

void GameScene::InitializeHitEffect() {
    if (!ctx_ || !ctx_->model || !ctx_->modelRenderer || !ctx_->texture) {
        return;
    }

    const std::filesystem::path texturePath = AssetPaths::kHitEffectTexture;
    if (!std::filesystem::exists(texturePath)) {
        return;
    }

    Material material{};
    material.color = {1.0f, 1.0f, 1.0f, 1.0f};
    material.enableTexture = 1;
    material.reflectionStrength = 0.0f;
    material.reflectionFresnelStrength = 0.0f;

    const uint32_t textureId = ctx_->texture->Load(texturePath.wstring());
    hitEffectModelId_ = ctx_->model->CreateRing(textureId, material, 32, 1.0f,
                                                0.2f);
    hasHitEffectModel_ = true;

    hitParticleSystem_.Initialize(ctx_->model, ctx_->modelRenderer,
                                  hitEffectModelId_);
    hitEmitter_.Initialize(&hitParticleSystem_, {0.0f, 1.2f, 0.0f});
    hitEmitter_.SetInterval(1.0f);
    hitEmitter_.SetAutoEmitEnabled(true);

    gpuParticleSystem_.Initialize(ctx_->dxCommon, ctx_->srv, ctx_->texture,
                                  textureId, 2048);
    gpuParticleSystem_.SetEmitterPosition({0.0f, 0.4f, 0.0f});
    hasGpuParticleSystem_ = true;
}

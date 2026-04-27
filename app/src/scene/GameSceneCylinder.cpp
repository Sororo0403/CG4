#include "AssetPaths.h"
#include "GameSceneCylinder.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#ifdef _DEBUG
#include "imgui.h"
#endif // _DEBUG

using namespace DirectX;

void GameSceneCylinder::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    const float aspect = static_cast<float>(ctx.winApp->GetWidth()) /
                         static_cast<float>(ctx.winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.SetMode(CameraMode::LookAt);
    UpdateCameraFromSimpleParams();
    camera_.UpdateMatrices();

    ctx.dxCommon->BeginUpload();
    InitializeSneakWalkTest();
    InitializeHitEffect();
    ctx.dxCommon->EndUpload();
    ctx.texture->ReleaseUploadBuffers();

    if (hasHitEffectModel_) {
        hitEmitter_.EmitNow();
    }
}

void GameSceneCylinder::Update() {
    const float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                         static_cast<float>((std::max)(ctx_->winApp->GetHeight(), 1));
    camera_.SetAspect(aspect);
    if (autoRotateCamera_) {
        cameraOrbitDegrees_ += autoRotateSpeedDeg_ * ctx_->deltaTime;
        if (cameraOrbitDegrees_ > 360.0f) {
            cameraOrbitDegrees_ -= 360.0f;
        }
    }
    UpdateCameraFromSimpleParams();
    camera_.UpdateMatrices();

    if (hasSneakWalkModel_ && animateSneakWalk_) {
        ctx_->model->UpdateAnimation(sneakWalkModelId_, ctx_->deltaTime);
    }

    if (ctx_->input->IsKeyTrigger(DIK_SPACE) || ctx_->input->IsMouseTrigger(0)) {
        hitEmitter_.EmitNow();
    }

    hitEmitter_.Update(ctx_->deltaTime);
    hitParticleSystem_.Update(ctx_->deltaTime);
}

void GameSceneCylinder::Draw() {
    if (!ctx_ || !ctx_->modelRenderer) {
        return;
    }

#ifdef _DEBUG
    if (ctx_->imgui) {
        static constexpr const char *kMaterialPresets[] = {
            "Matte", "Standard", "Metal"};

        ImGui::Begin("SneakWalk Test");
        ImGui::TextUnformatted("Easy Controls");
        ImGui::Checkbox("Animate", &animateSneakWalk_);
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            sneakWalkTransform_.position = {0.0f, 0.0f, 0.0f};
            sneakWalkTransform_.scale = {1.0f, 1.0f, 1.0f};
            sneakWalkYaw_ = 0.0f;
            materialPresetIndex_ = 1;
            ApplyMaterialPreset(materialPresetIndex_);
            cameraOrbitDegrees_ = 180.0f;
            cameraDistance_ = 5.0f;
            cameraHeight_ = 1.5f;
            cameraTargetHeight_ = 1.0f;
            autoRotateCamera_ = false;
        }

        ImGui::SeparatorText("Material");
        if (ImGui::Combo("Preset", &materialPresetIndex_, kMaterialPresets,
                         IM_ARRAYSIZE(kMaterialPresets))) {
            ApplyMaterialPreset(materialPresetIndex_);
        }
        ImGui::SliderFloat("Reflect", &sneakWalkReflectionStrength_, 0.0f, 1.0f);
        ImGui::SliderFloat("Fresnel", &sneakWalkReflectionFresnelStrength_, 0.0f,
                           2.0f);
        ImGui::SliderFloat("Roughness", &sneakWalkReflectionRoughness_, 0.0f,
                           1.0f);

        ImGui::SeparatorText("Model");
        ImGui::Checkbox("Show Skeleton", &showSkeleton_);
        ImGui::DragFloat3("Position", &sneakWalkTransform_.position.x, 0.01f,
                          -10.0f, 10.0f);
        ImGui::SliderAngle("Yaw", &sneakWalkYaw_, -180.0f, 180.0f);
        ImGui::DragFloat3("Scale", &sneakWalkTransform_.scale.x, 0.01f, 0.01f,
                          10.0f);

        ImGui::SeparatorText("Camera");
        ImGui::SliderFloat("Distance", &cameraDistance_, 2.0f, 12.0f);
        ImGui::SliderFloat("Height", &cameraHeight_, 0.2f, 6.0f);
        ImGui::SliderFloat("Target Height", &cameraTargetHeight_, 0.0f, 3.0f);
        ImGui::SliderFloat("Orbit", &cameraOrbitDegrees_, 0.0f, 360.0f);
        ImGui::Checkbox("Auto Orbit", &autoRotateCamera_);
        ImGui::SliderFloat("Orbit Speed", &autoRotateSpeedDeg_, -180.0f, 180.0f);

        ImGui::SeparatorText("Environment");
        ImGui::Checkbox("Use Per-Model EnvMap",
                        &usePerModelEnvironmentTexture_);

        ImGui::End();
    }
#endif // _DEBUG

    ApplySneakWalkMaterialParams();

    ctx_->modelRenderer->PreDraw();
    if (hasSneakWalkModel_) {
        const Model *sneakWalkModel = ctx_->model->GetModel(sneakWalkModelId_);
        if (sneakWalkModel) {
            if (usePerModelEnvironmentTexture_ && hasEnvironmentTexture_) {
                ctx_->modelRenderer->Draw(*sneakWalkModel, sneakWalkTransform_,
                                          camera_, environmentTextureId_);
            } else {
                ctx_->modelRenderer->Draw(*sneakWalkModel, sneakWalkTransform_,
                                          camera_);
            }
        }
    }
    hitParticleSystem_.Draw(camera_);
    if (hasSneakWalkModel_ && showSkeleton_) {
        ctx_->model->DrawSkeleton(sneakWalkModelId_, sneakWalkTransform_, camera_);
    }
    ctx_->modelRenderer->PostDraw();
}

void GameSceneCylinder::InitializeHitEffect() {
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
    hitEffectModelId_ =
        ctx_->model->CreateCylinder(textureId, material, 32, 1.0f, 1.0f, 3.0f);
    hasHitEffectModel_ = true;

    hitParticleSystem_.Initialize(ctx_->model, ctx_->modelRenderer,
                                  hitEffectModelId_);
    hitEmitter_.Initialize(&hitParticleSystem_, {0.0f, 1.2f, 0.0f});
    hitEmitter_.SetInterval(1.0f);
    hitEmitter_.SetAutoEmitEnabled(true);
}

void GameSceneCylinder::InitializeSneakWalkTest() {
    if (!ctx_ || !ctx_->model || !ctx_->texture) {
        return;
    }

    std::filesystem::path modelPath = AssetPaths::kSneakWalkModel;
    if (!std::filesystem::exists(modelPath)) {
        modelPath = AssetPaths::kAnimatedCubeModel;
    }

    if (std::filesystem::exists(modelPath)) {
        sneakWalkModelId_ = ctx_->model->Load(modelPath.wstring());
        hasSneakWalkModel_ = true;
        sneakWalkTransform_.position = {0.0f, 0.0f, 0.0f};
        XMStoreFloat4(&sneakWalkTransform_.rotation,
                      XMQuaternionIdentity());
        sneakWalkTransform_.scale = {1.0f, 1.0f, 1.0f};

        ApplyMaterialPreset(materialPresetIndex_);
        ApplySneakWalkMaterialParams();
    }

    const std::filesystem::path envPath = L"app/resources/rostock_laage_airport_4k.dds";
    if (std::filesystem::exists(envPath)) {
        environmentTextureId_ = ctx_->texture->Load(envPath.wstring());
        hasEnvironmentTexture_ = true;
    }
}

void GameSceneCylinder::ApplySneakWalkMaterialParams() {
    if (!ctx_ || !ctx_->model || !hasSneakWalkModel_) {
        return;
    }

    Model *sneakWalkModel = ctx_->model->GetModel(sneakWalkModelId_);
    if (!sneakWalkModel) {
        return;
    }

    XMStoreFloat4(&sneakWalkTransform_.rotation,
                  XMQuaternionRotationRollPitchYaw(0.0f, sneakWalkYaw_, 0.0f));

    for (const ModelSubMesh &subMesh : sneakWalkModel->subMeshes) {
        Material material = ctx_->model->GetMaterial(subMesh.materialId);
        material.reflectionStrength = sneakWalkReflectionStrength_;
        material.reflectionFresnelStrength = sneakWalkReflectionFresnelStrength_;
        material.reflectionRoughness = sneakWalkReflectionRoughness_;
        ctx_->model->SetMaterial(subMesh.materialId, material);
    }
}

void GameSceneCylinder::ApplyMaterialPreset(int presetIndex) {
    switch (presetIndex) {
    case 0: // Matte
        sneakWalkReflectionStrength_ = 0.05f;
        sneakWalkReflectionFresnelStrength_ = 0.08f;
        sneakWalkReflectionRoughness_ = 0.85f;
        break;
    case 2: // Metal
        sneakWalkReflectionStrength_ = 0.55f;
        sneakWalkReflectionFresnelStrength_ = 0.45f;
        sneakWalkReflectionRoughness_ = 0.08f;
        break;
    case 1: // Standard
    default:
        sneakWalkReflectionStrength_ = 0.18f;
        sneakWalkReflectionFresnelStrength_ = 0.12f;
        sneakWalkReflectionRoughness_ = 0.35f;
        break;
    }
}

void GameSceneCylinder::UpdateCameraFromSimpleParams() {
    const float orbitRadians = XMConvertToRadians(cameraOrbitDegrees_);
    const float orbitX = std::sinf(orbitRadians) * cameraDistance_;
    const float orbitZ = std::cosf(orbitRadians) * cameraDistance_;
    cameraTarget_ = {0.0f, cameraTargetHeight_, 0.0f};
    camera_.SetPosition({orbitX, cameraHeight_, orbitZ});
    camera_.LookAt(cameraTarget_);
}

#include "GameScene.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "ModelManager.h"
#include "ModelRenderer.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <DirectXMath.h>
#include <algorithm>
#include <filesystem>
#include <numbers>
#include <random>
#include <stdexcept>

#ifdef _DEBUG
#include "imgui.h"
#endif // _DEBUG

using namespace DirectX;

namespace {

XMMATRIX TransformToMatrix(const Transform &transform) {
    const XMVECTOR rotation =
        XMQuaternionNormalize(XMLoadFloat4(&transform.rotation));

    return XMMatrixScaling(transform.scale.x, transform.scale.y,
                           transform.scale.z) *
           XMMatrixRotationQuaternion(rotation) *
           XMMatrixTranslation(transform.position.x, transform.position.y,
                               transform.position.z);
}

Transform MatrixToTransform(const XMMATRIX &matrix) {
    XMVECTOR scale;
    XMVECTOR rotation;
    XMVECTOR translation;
    if (!XMMatrixDecompose(&scale, &rotation, &translation, matrix)) {
        throw std::runtime_error("Failed to decompose transform");
    }

    Transform transform{};
    XMStoreFloat3(&transform.scale, scale);
    XMStoreFloat4(&transform.rotation, XMQuaternionNormalize(rotation));
    XMStoreFloat3(&transform.position, translation);
    return transform;
}

} // namespace

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
}

void GameScene::Update() {
    const float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                         static_cast<float>((std::max)(ctx_->winApp->GetHeight(), 1));
    camera_.SetAspect(aspect);
    camera_.Update(*ctx_->input, ctx_->deltaTime);
    camera_.UpdateMatrices();

    UpdateHitEffect(ctx_->deltaTime);

    if (ctx_->input->IsKeyTrigger(DIK_SPACE) || ctx_->input->IsMouseTrigger(0)) {
        SpawnHitEffect({0.0f, 1.2f, 0.0f});
    }

    hitEffectAutoSpawnTimer_ += ctx_->deltaTime;
    if (hitEffectAutoSpawnTimer_ >= 1.0f) {
        hitEffectAutoSpawnTimer_ = 0.0f;
        SpawnHitEffect({0.0f, 1.2f, 0.0f});
    }
}

void GameScene::Draw() {
    ctx_->modelRenderer->PreDraw();
    DrawHitEffect();

    ctx_->modelRenderer->PostDraw();
}

uint32_t GameScene::LoadSkyboxTexture() {
    static constexpr const wchar_t *kCandidates[] = {
        L"resources/skybox/rostock_laage_airport_4k.dds",
        L"resources/textures/rostock_laage_airport_4k.dds",
        L"resources/rostock_laage_airport_4k.dds",
    };

    for (const wchar_t *path : kCandidates) {
        if (std::filesystem::exists(path)) {
            return ctx_->texture->Load(
                L"resources/rostock_laage_airport_4k.dds");
        }
    }

    return ctx_->texture->CreateDebugCubemap(8);
}

void GameScene::LoadLevel(const std::filesystem::path &path) {
    placedObjects_.clear();
    modelCache_.clear();

    const LevelData level = LevelLoader::Load(path);
    const std::filesystem::path baseDirectory = path.parent_path();

    for (const LevelObjectData &object : level.objects) {
        InstantiateLevelObject(object, baseDirectory, XMMatrixIdentity());
    }
}

void GameScene::InstantiateLevelObject(
    const LevelObjectData &object, const std::filesystem::path &baseDirectory,
    const XMMATRIX &parentWorld) {
    const XMMATRIX localWorld = TransformToMatrix(object.transform);
    const XMMATRIX world = localWorld * parentWorld;

    if (object.type == "MESH" && !object.fileName.empty()) {
        const std::filesystem::path modelPath = baseDirectory / object.fileName;
        const std::wstring fullPath = modelPath.lexically_normal().wstring();

        uint32_t modelId = 0;
        auto it = modelCache_.find(fullPath);
        if (it == modelCache_.end()) {
            modelId = ctx_->model->Load(fullPath);
            modelCache_.emplace(fullPath, modelId);
        } else {
            modelId = it->second;
        }

        placedObjects_.push_back(
            {object.name, fullPath, modelId, MatrixToTransform(world)});
    }

    for (const LevelObjectData &child : object.children) {
        InstantiateLevelObject(child, baseDirectory, world);
    }
}

void GameScene::InitializeHitEffect() {
    if (!ctx_ || !ctx_->model || !ctx_->texture) {
        return;
    }

    const std::filesystem::path texturePath =
        L"resources/textures/gradationLine.png";
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
    hitParticles_.resize(64);
    SpawnHitEffect({0.0f, 1.2f, 0.0f});
}

void GameScene::SpawnHitEffect(const XMFLOAT3 &position) {
    if (!hasHitEffectModel_) {
        return;
    }

    std::uniform_real_distribution<float> distJitter(-0.18f, 0.18f);
    std::uniform_real_distribution<float> distLife(0.42f, 0.62f);
    std::uniform_real_distribution<float> distAngularVelocity(-0.45f, 0.45f);

    int spawnedCount = 0;
    for (HitParticle &particle : hitParticles_) {
        if (particle.isAlive) {
            continue;
        }

        particle.isAlive = true;
        particle.transform.position = position;
        particle.life = 0.0f;
        particle.maxLife = distLife(randomEngine_);

        if (spawnedCount == 0) {
            // 中央の柔らかいハロー
            particle.baseScale = {0.42f, 0.42f, 1.0f};
            particle.roll = 0.0f;
            particle.angularVelocity = 0.18f;
        } else {
            // 細いリングを放射状に重ねて星型シルエットを作る
            const float baseAngle =
                (std::numbers::pi_v<float> / 5.0f) *
                static_cast<float>(spawnedCount - 1);
            particle.baseScale = {0.055f, 0.98f, 1.0f};
            particle.roll = baseAngle + distJitter(randomEngine_);
            particle.angularVelocity = distAngularVelocity(randomEngine_);
        }
        particle.transform.scale = particle.baseScale;

        ++spawnedCount;
        if (spawnedCount >= 6) {
            break;
        }
    }
}

void GameScene::UpdateHitEffect(float deltaTime) {
    for (HitParticle &particle : hitParticles_) {
        if (!particle.isAlive) {
            continue;
        }

        particle.life += deltaTime;
        if (particle.life >= particle.maxLife) {
            particle.isAlive = false;
            continue;
        }

        const float t = particle.life / particle.maxLife;
        particle.roll += particle.angularVelocity * deltaTime;
        const bool isCore = particle.baseScale.x > 0.2f;

        if (isCore) {
            const float expand = 1.0f + t * 0.30f;
            particle.transform.scale.x = particle.baseScale.x * expand;
            particle.transform.scale.y = particle.baseScale.y * expand;
        } else {
            const float expandX = 1.0f + t * 0.12f;
            const float expandY = 1.0f + t * 0.55f;
            particle.transform.scale.x = particle.baseScale.x * expandX;
            particle.transform.scale.y = particle.baseScale.y * expandY;
        }
        particle.transform.scale.z = particle.baseScale.z;
    }
}

void GameScene::DrawHitEffect() {
    if (!hasHitEffectModel_ || !ctx_ || !ctx_->model || !ctx_->modelRenderer) {
        return;
    }

    const Model *effectModel = ctx_->model->GetModel(hitEffectModelId_);
    if (!effectModel || effectModel->subMeshes.empty()) {
        return;
    }

    const uint32_t materialId = effectModel->subMeshes.front().materialId;
    const Material baseMaterial = ctx_->model->GetMaterial(materialId);

    XMMATRIX billboard = camera_.GetView();
    billboard.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    billboard = XMMatrixInverse(nullptr, billboard);

    ModelDrawEffect drawEffect{};
    drawEffect.enabled = true;
    drawEffect.additiveBlend = true;
    drawEffect.color = {0.95f, 0.95f, 1.0f, 0.70f};
    drawEffect.intensity = 0.14f;
    drawEffect.fresnelPower = 2.0f;
    drawEffect.noiseAmount = 0.0f;
    drawEffect.time = hitEffectAutoSpawnTimer_;
    ctx_->modelRenderer->SetDrawEffect(drawEffect);

    for (const HitParticle &particle : hitParticles_) {
        if (!particle.isAlive) {
            continue;
        }

        const float t = particle.life / particle.maxLife;
        const bool isCore = particle.baseScale.x > 0.2f;
        const float fade = 1.0f - t;
        const float alpha = isCore ? fade * 0.62f : fade * 0.92f;

        Material material = baseMaterial;
        material.color = {1.0f, 0.98f, 0.90f, alpha};
        material.reflectionStrength = 0.0f;
        material.reflectionFresnelStrength = 0.0f;
        const float uvScaleV = isCore ? 2.3f : 8.5f;
        const float uvScroll = t * (isCore ? 0.7f : 2.1f);
        const XMMATRIX uvTransform =
            XMMatrixScaling(1.0f, uvScaleV, 1.0f) *
            XMMatrixTranslation(0.0f, uvScroll, 0.0f);
        XMStoreFloat4x4(&material.uvTransform, XMMatrixTranspose(uvTransform));
        ctx_->model->SetMaterial(materialId, material);

        Transform drawTransform = particle.transform;
        const XMMATRIX rotationMatrix = XMMatrixRotationZ(particle.roll) * billboard;
        XMStoreFloat4(&drawTransform.rotation, XMQuaternionNormalize(
                                                   XMQuaternionRotationMatrix(
                                                       rotationMatrix)));

        ctx_->modelRenderer->Draw(*effectModel, drawTransform, camera_);
    }

    ctx_->model->SetMaterial(materialId, baseMaterial);
    ctx_->modelRenderer->ClearDrawEffect();
}

#ifdef _DEBUG
void GameScene::DrawDebugUi() {
    if (!ctx_ || !ctx_->model || !ctx_->modelRenderer) {
        return;
    }

    ImGui::Begin("Model Debug");

    ImGui::Text("sneakWalk");
    ImGui::Separator();

    ImGui::DragFloat3("Position", &sneakWalkTransform_.position.x, 0.05f);
    ImGui::DragFloat3("Scale", &sneakWalkTransform_.scale.x, 0.01f, 0.01f,
                      10.0f);

    if (ImGui::CollapsingHeader("Scene Lighting",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Key Direction", &sceneLighting_.keyLightDirection.x,
                          0.01f, -1.0f, 1.0f);
        ImGui::ColorEdit3("Key Color", &sceneLighting_.keyLightColor.x);
        ImGui::DragFloat3("Fill Direction",
                          &sceneLighting_.fillLightDirection.x, 0.01f, -1.0f,
                          1.0f);
        ImGui::ColorEdit3("Fill Color", &sceneLighting_.fillLightColor.x);
        ImGui::SliderFloat("Fill Rim", &sceneLighting_.fillLightColor.w, 0.0f,
                           2.0f);
        ImGui::ColorEdit3("Ambient", &sceneLighting_.ambientColor.x);
        ImGui::SliderFloat("Spec Power", &sceneLighting_.lightingParams.x, 1.0f,
                           128.0f);
        ImGui::SliderFloat("Spec Strength",
                           &sceneLighting_.lightingParams.y, 0.0f, 2.0f);
        ImGui::SliderFloat("Rim Power", &sceneLighting_.lightingParams.z, 0.5f,
                           8.0f);
        ImGui::SliderFloat("Wrap", &sceneLighting_.lightingParams.w, 0.0f,
                           1.0f);

        for (size_t lightIndex = 0; lightIndex < sceneLighting_.pointLights.size();
             ++lightIndex) {
            PointLight &pointLight = sceneLighting_.pointLights[lightIndex];
            const std::string lightLabel =
                "Point Light " + std::to_string(lightIndex);
            if (ImGui::TreeNode(lightLabel.c_str())) {
                ImGui::DragFloat3("Position##PointLight",
                                  &pointLight.positionRange.x, 0.05f);
                ImGui::SliderFloat("Range##PointLight",
                                   &pointLight.positionRange.w, 0.1f,
                                   30.0f);
                ImGui::ColorEdit3("Color##PointLight",
                                  &pointLight.colorIntensity.x);
                ImGui::SliderFloat("Intensity##PointLight",
                                   &pointLight.colorIntensity.w, 0.0f, 5.0f);
                ImGui::TreePop();
            }
        }

        ctx_->modelRenderer->SetSceneLighting(sceneLighting_);
    }

    if (hasSneakWalkModel_) {
        const Model *model = ctx_->model->GetModel(sneakWalkModelId_);
        if (model && ImGui::CollapsingHeader("Materials",
                                             ImGuiTreeNodeFlags_DefaultOpen)) {
            for (size_t subMeshIndex = 0; subMeshIndex < model->subMeshes.size();
                 ++subMeshIndex) {
                const ModelSubMesh &subMesh = model->subMeshes[subMeshIndex];
                Material material = ctx_->model->GetMaterial(subMesh.materialId);
                const std::string header =
                    "SubMesh " + std::to_string(subMeshIndex);

                if (!ImGui::TreeNode(header.c_str())) {
                    continue;
                }

                bool changed = false;
                bool enableTexture = material.enableTexture != 0;
                changed |= ImGui::ColorEdit4("Base Color", &material.color.x);
                changed |= ImGui::Checkbox("Enable Texture", &enableTexture);
                changed |= ImGui::SliderFloat("Reflection",
                                              &material.reflectionStrength, 0.0f,
                                              1.0f);
                changed |= ImGui::SliderFloat(
                    "Fresnel Reflection",
                    &material.reflectionFresnelStrength, 0.0f, 1.0f);

                if (changed) {
                    material.enableTexture = enableTexture ? 1 : 0;
                    ctx_->model->SetMaterial(subMesh.materialId, material);
                }

                ImGui::TreePop();
            }
        }
    }

    ImGui::End();
}
#endif // _DEBUG

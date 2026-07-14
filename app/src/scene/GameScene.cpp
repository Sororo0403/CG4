#include "scene/GameScene.h"

#include "animation/SkeletonPoseBuilder.h"

#ifdef _DEBUG
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace DirectX;

namespace {

constexpr float kMoveSpeed = 2.2f;
constexpr float kWeaponHandleLength = 0.52f;
constexpr float kWeaponGuardWidth = 0.38f;
constexpr float kWeaponGuardThickness = 0.055f;
constexpr float kWeaponGuardDepth = 0.055f;
constexpr float kWeaponBladeWidth = 0.08f;
constexpr float kWeaponBladeLength = 0.72f;
constexpr float kWeaponBladeDepth = 0.035f;
constexpr float kWeaponJointOverlap = 0.006f;

Material MakeMaterial(const XMFLOAT4 &color, float metallic = 0.0f,
                      float roughness = 0.55f) {
    Material material{};
    material.color = color;
    material.enableTexture = 1;
    material.metallic = metallic;
    material.roughness = roughness;
    material.reflectionStrength = metallic > 0.0f ? 0.28f : 0.08f;
    return material;
}

Material MakeDebugMaterial(const XMFLOAT4 &color) {
    XMFLOAT4 visibleColor = color;
    visibleColor.w = 1.0f;
    Material material = MakeMaterial(visibleColor, 0.0f, 0.32f);
    material.blendMode = static_cast<int32_t>(BlendMode::Opaque);
    material.depthWrite = 0;
    material.cullMode = static_cast<int32_t>(MaterialCullMode::None);
    return material;
}

XMFLOAT4 QuaternionFromYaw(float yaw) {
    XMFLOAT4 rotation{};
    XMStoreFloat4(&rotation, XMQuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f));
    return rotation;
}

float LengthSq(float x, float z) {
    return x * x + z * z;
}

XMFLOAT3 Add(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

XMFLOAT3 Sub(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

XMFLOAT3 Scale(const XMFLOAT3 &v, float scale) {
    return {v.x * scale, v.y * scale, v.z * scale};
}

float Dot(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

XMFLOAT3 Cross(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    XMFLOAT3 result{};
    XMStoreFloat3(&result,
                  XMVector3Cross(XMLoadFloat3(&a), XMLoadFloat3(&b)));
    return result;
}

XMFLOAT3 NormalizeOr(const XMFLOAT3 &v, const XMFLOAT3 &fallback) {
    const XMVECTOR vector = XMLoadFloat3(&v);
    const float lengthSq = XMVectorGetX(XMVector3LengthSq(vector));
    if (!std::isfinite(lengthSq) || lengthSq <= 0.000001f) {
        return fallback;
    }

    XMFLOAT3 result{};
    XMStoreFloat3(&result, XMVector3Normalize(vector));
    return result;
}

bool ContainsAny(const std::string &text,
                 std::initializer_list<const char *> needles) {
    for (const char *needle : needles) {
        if (text.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

const AnimationClip *FirstAnimationClip(const Model &model) {
    if (model.animations.empty()) {
        return nullptr;
    }
    if (!model.currentAnimation.empty()) {
        auto found = model.animations.find(model.currentAnimation);
        if (found != model.animations.end()) {
            return &found->second;
        }
    }
    return &model.animations.begin()->second;
}

float WrapAnimationTime(float time, float duration) {
    if (!std::isfinite(duration) || duration <= 0.0f) {
        return 0.0f;
    }
    if (!std::isfinite(time) || time < 0.0f) {
        time = 0.0f;
    }
    return std::fmod(time, duration);
}

XMMATRIX BlendLocalMatrix(const XMMATRIX &walk, const XMMATRIX &sneak,
                          float blend) {
    blend = std::clamp(blend, 0.0f, 1.0f);
    if (blend <= 0.0001f) {
        return walk;
    }
    if (blend >= 0.9999f) {
        return sneak;
    }

    XMVECTOR walkScale{};
    XMVECTOR walkRotation{};
    XMVECTOR walkTranslation{};
    XMVECTOR sneakScale{};
    XMVECTOR sneakRotation{};
    XMVECTOR sneakTranslation{};
    if (!XMMatrixDecompose(&walkScale, &walkRotation, &walkTranslation, walk) ||
        !XMMatrixDecompose(&sneakScale, &sneakRotation, &sneakTranslation,
                           sneak)) {
        return walk;
    }

    const XMVECTOR scale = XMVectorLerp(walkScale, sneakScale, blend);
    const XMVECTOR rotation =
        XMQuaternionSlerp(XMQuaternionNormalize(walkRotation),
                          XMQuaternionNormalize(sneakRotation), blend);
    const XMVECTOR translation =
        XMVectorLerp(walkTranslation, sneakTranslation, blend);
    return XMMatrixScalingFromVector(scale) *
           XMMatrixRotationQuaternion(rotation) *
           XMMatrixTranslationFromVector(translation);
}

XMFLOAT4 AlignYAxisTo(const XMFLOAT3 &direction) {
    XMVECTOR to = XMVector3Normalize(XMLoadFloat3(&direction));
    XMVECTOR from = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const float dot = std::clamp(XMVectorGetX(XMVector3Dot(from, to)), -1.0f,
                                 1.0f);

    if (dot > 0.9995f) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }

    const XMVECTOR quat =
        dot < -0.9995f
            ? XMQuaternionRotationAxis(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XM_PI)
            : XMQuaternionRotationAxis(XMVector3Normalize(XMVector3Cross(from, to)),
                                       std::acos(dot));

    XMFLOAT4 result{};
    XMStoreFloat4(&result, quat);
    return result;
}

XMFLOAT4 QuaternionFromAxes(const XMFLOAT3& xAxis, const XMFLOAT3& yAxis,
                            const XMFLOAT3& zAxis) {
    const XMMATRIX rotation = XMMatrixSet(
        xAxis.x, xAxis.y, xAxis.z, 0.0f, yAxis.x, yAxis.y, yAxis.z, 0.0f,
        zAxis.x, zAxis.y, zAxis.z, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    XMFLOAT4 result{};
    XMStoreFloat4(&result, XMQuaternionRotationMatrix(rotation));
    return result;
}

std::vector<ParticleMeshTriangle> MakeOctahedronParticleMesh() {
    constexpr XMFLOAT3 top{0.0f, 1.0f, 0.0f};
    constexpr XMFLOAT3 bottom{0.0f, -1.0f, 0.0f};
    constexpr XMFLOAT3 right{1.0f, 0.0f, 0.0f};
    constexpr XMFLOAT3 left{-1.0f, 0.0f, 0.0f};
    constexpr XMFLOAT3 front{0.0f, 0.0f, 1.0f};
    constexpr XMFLOAT3 back{0.0f, 0.0f, -1.0f};
    return {{top, right, front},    {top, front, left},
            {top, left, back},      {top, back, right},
            {bottom, front, right}, {bottom, left, front},
            {bottom, back, left},   {bottom, right, back}};
}

} // namespace

GameScene::~GameScene() {
    if (ctx_ != nullptr && ctx_->rendering.dxCommon != nullptr) {
        ctx_->rendering.dxCommon->WaitForGpuIfPossible();
    }
    handParticles_.Release();
}

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    InitializeCamera();
    InitializeModels();
    InitializeParticles();
}

void GameScene::InitializeCamera() {
    float aspect = 16.0f / 9.0f;
    if (ctx_ != nullptr && ctx_->systems.winApp != nullptr &&
        ctx_->systems.winApp->GetHeight() > 0) {
        aspect = static_cast<float>(ctx_->systems.winApp->GetWidth()) /
                 static_cast<float>(ctx_->systems.winApp->GetHeight());
    }

    camera_.Initialize(aspect);
    camera_.SetPosition({0.0f, 1.45f, -5.0f});
    camera_.SetRotation({0.08f, 0.0f, 0.0f});
    camera_.SetPerspectiveFovDeg(47.0f);
    camera_.SetClipRange(0.01f, 120.0f);
}

void GameScene::InitializeModels() {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        ctx_->rendering.texture == nullptr) {
        return;
    }

    whiteTextureId_ =
        ctx_->rendering.texture->Load(L"app/resources/models/human/white.png");

    humanModelId_ =
        ctx_->rendering.model->Load(L"app/resources/models/human/walk.gltf");
    sneakModelId_ = ctx_->rendering.model->Load(
        L"app/resources/models/human/sneakWalk.gltf");
    brainStemModelId_ = ctx_->rendering.model->Load(
        L"app/resources/models/brainStem/BrainStem.glb");

    Material groundMaterial =
        MakeMaterial({0.28f, 0.34f, 0.32f, 1.0f}, 0.0f, 0.82f);
    groundModelId_ = ctx_->rendering.model->CreateBox(
        whiteTextureId_, groundMaterial, 7.0f, 0.06f, 7.0f);

    Material gripMaterial =
        MakeMaterial({0.15f, 0.10f, 0.07f, 1.0f}, 0.0f, 0.48f);
    Material guardMaterial =
        MakeMaterial({0.86f, 0.66f, 0.28f, 1.0f}, 0.65f, 0.25f);
    Material bladeMaterial =
        MakeMaterial({0.72f, 0.82f, 0.92f, 1.0f}, 0.9f, 0.18f);
    Material boneMaterial = MakeDebugMaterial({0.2f, 1.0f, 0.36f, 0.82f});
    Material boneCenterMaterial =
        MakeDebugMaterial({0.82f, 0.84f, 0.88f, 0.72f});
    Material boneLeftMaterial =
        MakeDebugMaterial({0.22f, 0.48f, 1.0f, 0.78f});
    Material boneRightMaterial =
        MakeDebugMaterial({1.0f, 0.24f, 0.18f, 0.78f});
    Material boneBindMaterial =
        MakeDebugMaterial({1.0f, 0.92f, 0.30f, 0.34f});
    Material boneSelectedMaterial =
        MakeDebugMaterial({1.0f, 0.88f, 0.18f, 0.94f});

    weaponHandleModelId_ = ctx_->rendering.model->CreateCylinder(
        whiteTextureId_, gripMaterial, 16u, 0.035f, 0.035f, kWeaponHandleLength);
    weaponGuardModelId_ = ctx_->rendering.model->CreateBox(
        whiteTextureId_, guardMaterial, kWeaponGuardWidth, kWeaponGuardThickness,
        kWeaponGuardDepth);
    weaponBladeModelId_ = ctx_->rendering.model->CreateBox(
        whiteTextureId_, bladeMaterial, kWeaponBladeWidth, kWeaponBladeLength,
        kWeaponBladeDepth);
    boneJointModelId_ = ctx_->rendering.model->CreateSphere(
        whiteTextureId_, boneMaterial, 12u, 6u, 1.0f);
    boneCenterModelId_ = ctx_->rendering.model->CreateCylinder(
        whiteTextureId_, boneCenterMaterial, 8u, 0.015f, 0.015f, 1.0f);
    boneLeftModelId_ = ctx_->rendering.model->CreateCylinder(
        whiteTextureId_, boneLeftMaterial, 8u, 0.018f, 0.018f, 1.0f);
    boneRightModelId_ = ctx_->rendering.model->CreateCylinder(
        whiteTextureId_, boneRightMaterial, 8u, 0.018f, 0.018f, 1.0f);
    boneBindModelId_ = ctx_->rendering.model->CreateCylinder(
        whiteTextureId_, boneBindMaterial, 8u, 0.010f, 0.010f, 1.0f);
    boneSelectedModelId_ = ctx_->rendering.model->CreateSphere(
        whiteTextureId_, boneSelectedMaterial, 16u, 8u, 1.0f);

    characterTransform_.position = {0.0f, 0.0f, 0.0f};
    characterTransform_.scale = {1.0f, 1.0f, 1.0f};

    const Model *model = ctx_->rendering.model->GetModel(humanModelId_);
    if (model != nullptr) {
        rightHandBone_ = FindBoneIndex({"mixamorig:RightHand", "RightHand"});
        leftHandBone_ = FindBoneIndex({"mixamorig:LeftHand", "LeftHand"});
        rightFootBone_ = FindBoneIndex({"mixamorig:RightFoot", "RightFoot"});
        leftFootBone_ = FindBoneIndex({"mixamorig:LeftFoot", "LeftFoot"});
        selectedBoneIndex_ = rightHandBone_ != kInvalidResourceId
                                 ? static_cast<int>(rightHandBone_)
                                 : (model->bones.empty() ? -1 : 0);

        const size_t boneCount = model->bones.size();
        bindPoseMatrices_.resize(boneCount);
        std::vector<XMMATRIX> globalMatrices(boneCount);
        for (size_t i = 0; i < boneCount; ++i) {
            XMMATRIX local = XMLoadFloat4x4(&model->bones[i].localBindMatrix);
            const int parentIndex = model->bones[i].parentIndex;
            if (parentIndex >= 0 &&
                static_cast<size_t>(parentIndex) < i) {
                globalMatrices[i] = local * globalMatrices[parentIndex];
            } else {
                globalMatrices[i] = local;
            }
            XMStoreFloat4x4(&bindPoseMatrices_[i], globalMatrices[i]);
        }
    }
}

void GameScene::InitializeParticles() {
    if (ctx_ == nullptr || ctx_->rendering.dxCommon == nullptr ||
        ctx_->rendering.srv == nullptr || ctx_->rendering.texture == nullptr) {
        return;
    }

    handParticlesReady_ = handParticles_.Initialize(
        ctx_->rendering.dxCommon, ctx_->rendering.srv, ctx_->rendering.texture,
        whiteTextureId_, 8192u);
    if (!handParticlesReady_) {
        return;
    }

    particleMaterial_.blendMode = GPUParticleMaterialSettings::BlendMode::Additive;
    particleMaterial_.params0 = {0.0f, 1.0f, 0.0f, 0.0f};
    handParticles_.SetMaterialSettings(particleMaterial_);
    handParticles_.SetLightingSettings(particleLighting_);

    rightParticleEmitter_.maxParticles = 8192u;
    rightParticleEmitter_.emissionType = ParticleEmissionType::Continuous;
    rightParticleEmitter_.spawnShape = ParticleSpawnShape::Sphere;
    rightParticleEmitter_.emitRate = 24.0f;
    rightParticleEmitter_.burstCount = 8u;
    rightParticleEmitter_.particlesPerThread = 4u;
    rightParticleEmitter_.spawnOffsetScale = {0.06f, 0.06f, 0.06f};
    rightParticleEmitter_.tintColor = {0.25f, 0.65f, 1.0f, 0.92f};
    rightParticleEmitter_.direction = {0.0f, 1.0f, 0.15f};
    rightParticleEmitter_.radialVelocity = 0.24f;
    rightParticleEmitter_.directionalVelocity = 0.32f;
    rightParticleEmitter_.baseLifeTime = 0.72f;
    rightParticleEmitter_.lifeTimeRandom = 0.22f;
    rightParticleEmitter_.startScale = 0.055f;
    rightParticleEmitter_.endScale = 0.0f;
    rightParticleEmitter_.scaleRandom = 0.025f;
    rightParticleEmitter_.stretch = 1.8f;
    rightParticleEmitter_.acceleration = {0.0f, 0.18f, 0.0f};
    rightParticleEmitter_.turbulence = 0.30f;
    rightParticleEmitter_.damping = 0.985f;
    rightParticleEmitter_.fadeInTime = 0.02f;
    rightParticleEmitter_.fadeOutTime = 0.24f;
    rightParticleEmitter_.lightInfluence = 1.0f;
    rightParticleEmitter_.assignedLight = 0u;
    rightParticleEmitter_.meshTriangles = MakeOctahedronParticleMesh();

    leftParticleEmitter_ = rightParticleEmitter_;
    leftParticleEmitter_.spawnShape = ParticleSpawnShape::Mesh;
    leftParticleEmitter_.spawnOffsetScale = {0.12f, 0.12f, 0.02f};
    leftParticleEmitter_.tintColor = {1.0f, 0.36f, 0.10f, 0.92f};
    leftParticleEmitter_.direction = {0.0f, 0.8f, -0.2f};
    leftParticleEmitter_.assignedLight = 1u;

    particleLighting_.pointLightCount = 2u;
    particleLighting_.pointLights[0].range = 1.25f;
    particleLighting_.pointLights[0].color = {0.20f, 0.55f, 1.0f, 1.0f};
    particleLighting_.pointLights[0].intensity = 2.4f;
    particleLighting_.pointLights[1].range = 1.25f;
    particleLighting_.pointLights[1].color = {1.0f, 0.28f, 0.06f, 1.0f};
    particleLighting_.pointLights[1].intensity = 2.4f;
    handParticles_.SetLightingSettings(particleLighting_);

    rightParticleEmitterId_ = handParticles_.AddEmitter(rightParticleEmitter_);
    leftParticleEmitterId_ = handParticles_.AddEmitter(leftParticleEmitter_);

    particleFields_.resize(4);
    particleFields_[0].type = ParticleFieldType::Directional;
    particleFields_[0].direction = {0.0f, 1.0f, 0.0f};
    particleFields_[0].strength = 0.12f;
    particleFields_[1].type = ParticleFieldType::Radial;
    particleFields_[1].enabled = false;
    particleFields_[1].radius = 2.0f;
    particleFields_[1].strength = 1.0f;
    particleFields_[2].type = ParticleFieldType::Vortex;
    particleFields_[2].enabled = false;
    particleFields_[2].radius = 2.0f;
    particleFields_[2].strength = 1.2f;
    particleFields_[3].type = ParticleFieldType::Drag;
    particleFields_[3].enabled = false;
    particleFields_[3].radius = 2.0f;
    particleFields_[3].strength = 1.5f;
    handParticles_.SetFields(particleFields_);
}

void GameScene::Update() {
    const float deltaTime =
        ctx_ != nullptr ? ctx_->frame.deltaTime : 1.0f / 60.0f;

    InitializeCamera();
    UpdateDebugControls();
    UpdateMovement(deltaTime);
    UpdateModelAnimation(deltaTime);
    UpdateAttachmentPoints();
    UpdateParticleEmitters();

    if (handParticlesReady_) {
        handParticles_.Update(deltaTime);
    }
}

void GameScene::UpdateMovement(float deltaTime) {
    if (ctx_ == nullptr || ctx_->systems.input == nullptr) {
        return;
    }

    float moveX = ctx_->systems.input->GetGamepadLeftStickX();
    float moveZ = ctx_->systems.input->GetGamepadLeftStickY();

    if (ctx_->systems.input->IsKeyPress(DIK_A)) {
        moveX -= 1.0f;
    }
    if (ctx_->systems.input->IsKeyPress(DIK_D)) {
        moveX += 1.0f;
    }
    if (ctx_->systems.input->IsKeyPress(DIK_W)) {
        moveZ += 1.0f;
    }
    if (ctx_->systems.input->IsKeyPress(DIK_S)) {
        moveZ -= 1.0f;
    }

    const float lenSq = LengthSq(moveX, moveZ);
    walkTimeScale_ = lenSq > 0.04f ? 1.0f : 0.0f;

    if (manualSneakBlend_ >= 0.0f) {
        sneakBlend_ = std::clamp(manualSneakBlend_, 0.0f, 1.0f);
    } else {
        const bool sneakHeld = ctx_->systems.input->IsKeyPress(DIK_LSHIFT) ||
                               ctx_->systems.input->IsKeyPress(DIK_RSHIFT);
        const float targetBlend = sneakHeld ? 1.0f : 0.0f;
        constexpr float blendSpeed = 9.0f;
        sneakBlend_ += (targetBlend - sneakBlend_) *
                       std::clamp(deltaTime * blendSpeed, 0.0f, 1.0f);
        if (std::abs(targetBlend - sneakBlend_) < 0.01f) {
            sneakBlend_ = targetBlend;
        }
    }

    if (lenSq <= 0.04f) {
        return;
    }

    const float invLen = 1.0f / std::sqrt(lenSq);
    moveX *= invLen;
    moveZ *= invLen;
    characterTransform_.position.x += moveX * kMoveSpeed * deltaTime;
    characterTransform_.position.z += moveZ * kMoveSpeed * deltaTime;
    characterYaw_ = std::atan2(moveX, moveZ);
    characterTransform_.rotation = QuaternionFromYaw(characterYaw_);
}

void GameScene::UpdateDebugControls() {
    if (ctx_ == nullptr || ctx_->systems.input == nullptr ||
        ctx_->rendering.model == nullptr) {
        return;
    }

    const Input *input = ctx_->systems.input;
    if (input->IsKeyTrigger(DIK_F1)) {
        debugRigEnabled_ = !debugRigEnabled_;
    }
    if (input->IsKeyTrigger(DIK_F3)) {
        debugLabelsEnabled_ = !debugLabelsEnabled_;
    }
    if (input->IsKeyTrigger(DIK_F5)) {
        debugBindPoseEnabled_ = !debugBindPoseEnabled_;
    }
    if (input->IsKeyTrigger(DIK_F6)) {
        debugMajorBonesOnly_ = !debugMajorBonesOnly_;
    }

    const Model *model = ctx_->rendering.model->GetModel(humanModelId_);
    if (model == nullptr || model->bones.empty()) {
        selectedBoneIndex_ = -1;
        return;
    }

    if (selectedBoneIndex_ < 0 ||
        selectedBoneIndex_ >= static_cast<int>(model->bones.size())) {
        selectedBoneIndex_ = 0;
    }

    if (input->IsKeyTrigger(DIK_RBRACKET)) {
        selectedBoneIndex_ =
            (selectedBoneIndex_ + 1) % static_cast<int>(model->bones.size());
    }
    if (input->IsKeyTrigger(DIK_LBRACKET)) {
        selectedBoneIndex_ =
            (selectedBoneIndex_ + static_cast<int>(model->bones.size()) - 1) %
            static_cast<int>(model->bones.size());
    }
}

void GameScene::UpdateModelAnimation(float deltaTime) {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        humanModelId_ == kInvalidResourceId) {
        return;
    }

    Model *humanModel = ctx_->rendering.model->GetModel(humanModelId_);
    const Model *sneakModel = ctx_->rendering.model->GetModel(sneakModelId_);
    if (humanModel == nullptr || sneakModel == nullptr) {
        ctx_->rendering.model->UpdateAnimation(humanModelId_,
                                               deltaTime * walkTimeScale_);
        return;
    }

    const AnimationClip *walkClip = FirstAnimationClip(*humanModel);
    const AnimationClip *sneakClip = FirstAnimationClip(*sneakModel);
    if (walkClip == nullptr || sneakClip == nullptr || humanModel->bones.empty()) {
        ctx_->rendering.model->UpdateAnimation(humanModelId_,
                                               deltaTime * walkTimeScale_);
        return;
    }

    const float walkDuration =
        std::isfinite(walkClip->duration) && walkClip->duration > 0.0f
            ? walkClip->duration
            : 1.0f;
    animationTime_ = WrapAnimationTime(animationTime_ + deltaTime * walkTimeScale_,
                                      walkDuration);
    const float walkTime = WrapAnimationTime(animationTime_, walkDuration);
    const float sneakTime =
        WrapAnimationTime(animationTime_ * (sneakClip->duration / walkDuration),
                          sneakClip->duration);

    std::vector<XMMATRIX> walkLocals;
    std::vector<XMMATRIX> sneakLocals;
    SkeletonPoseBuilder::BuildAnimatedLocals(*humanModel, *walkClip, walkTime,
                                             walkLocals);
    SkeletonPoseBuilder::BuildAnimatedLocals(*humanModel, *sneakClip, sneakTime,
                                             sneakLocals);

    if (walkLocals.size() != sneakLocals.size()) {
        SkeletonPoseBuilder::UpdateSkeleton(*humanModel, walkLocals);
        ctx_->rendering.model->GetRenderer()->UpdateSkinClusters(*humanModel);
        humanModel->animationTime = walkTime;
        return;
    }

    const float blend = std::clamp(sneakBlend_, 0.0f, 1.0f);
    std::vector<XMMATRIX> blendedLocals(walkLocals.size());
    for (size_t i = 0; i < walkLocals.size(); ++i) {
        blendedLocals[i] = BlendLocalMatrix(walkLocals[i], sneakLocals[i], blend);
    }

    SkeletonPoseBuilder::UpdateSkeleton(*humanModel, blendedLocals);
    ctx_->rendering.model->GetRenderer()->UpdateSkinClusters(*humanModel);
    humanModel->animationTime = walkTime;
    humanModel->hasRootAnimation = false;
    XMStoreFloat4x4(&humanModel->rootAnimationMatrix, XMMatrixIdentity());
}

void GameScene::UpdateAttachmentPoints() {
    rightHandWorld_ = BoneWorldPosition(rightHandBone_);
    leftHandWorld_ = BoneWorldPosition(leftHandBone_);
    rightFootWorld_ = BoneWorldPosition(rightFootBone_);
    leftFootWorld_ = BoneWorldPosition(leftFootBone_);
}

void GameScene::UpdateParticleEmitters() {
    if (!handParticlesReady_) {
        return;
    }
    rightParticleEmitter_.position = rightHandWorld_;
    leftParticleEmitter_.position = leftHandWorld_;
    handParticles_.UpdateEmitter(rightParticleEmitterId_, rightParticleEmitter_);
    handParticles_.UpdateEmitter(leftParticleEmitterId_, leftParticleEmitter_);
    particleLighting_.pointLights[0].position = rightHandWorld_;
    particleLighting_.pointLights[1].position = leftHandWorld_;
    handParticles_.SetLightingSettings(particleLighting_);

    const XMFLOAT3 fieldCenter = Add(characterTransform_.position, {0.0f, 0.9f, 0.0f});
    for (ParticleFieldSettings& field : particleFields_) {
        field.position = fieldCenter;
    }
    handParticles_.SetFields(particleFields_);
}

void GameScene::SubmitLighting(LightingScene &lightingScene) {
    SceneLighting lighting{};
    lighting.keyLightDirection = {-0.35f, -0.95f, 0.25f};
    lighting.keyLightColor = {1.18f, 1.10f, 0.96f, 1.0f};
    lighting.fillLightColor = {0.25f, 0.38f, 0.52f, 0.44f};
    lighting.ambientColor = {0.22f, 0.24f, 0.28f, 1.0f};
    lighting.pointLights[0] = {{rightHandWorld_.x, rightHandWorld_.y,
                                rightHandWorld_.z, 5.0f},
                               {0.25f, 0.62f, 1.0f, 1.7f}};
    lighting.pointLights[1] = {{0.0f, 2.1f, -1.8f, 7.0f},
                               {1.0f, 0.64f, 0.32f, 0.7f}};
    lightingScene.SetSceneLighting(lighting);
}

void GameScene::Draw() {
    DrawSceneProps();
    DrawCharacter();
    DrawWeapon();
}

void GameScene::DrawTransparent() {
    if (handParticlesReady_) {
        handParticles_.Draw(camera_);
    }
}

void GameScene::DrawPostProcessOverlay() {
    DrawBoneDebugOverlay();
    DrawBoneLabels();
    DrawDebugPanel();
    DrawParticleEditor();
}

void GameScene::DrawCharacter() {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        humanModelId_ == kInvalidResourceId) {
        return;
    }
    ctx_->rendering.model->Draw(humanModelId_, characterTransform_, camera_);
}

void GameScene::DrawWeapon() {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        humanModelId_ == kInvalidResourceId ||
        rightHandBone_ == kInvalidResourceId) {
        return;
    }

    const Model *model = ctx_->rendering.model->GetModel(humanModelId_);
    if (model == nullptr || rightHandBone_ >= model->bones.size()) {
        return;
    }

    XMFLOAT3 forearmWorld = Add(rightHandWorld_, {-1.0f, 0.0f, 0.0f});
    const int parentIndex = model->bones[rightHandBone_].parentIndex;
    if (parentIndex >= 0 &&
        parentIndex < static_cast<int>(model->bones.size())) {
        forearmWorld = BoneWorldPosition(static_cast<uint32_t>(parentIndex));
    }

    const XMFLOAT3 armDir =
        NormalizeOr(Sub(rightHandWorld_, forearmWorld), {1.0f, 0.0f, 0.0f});
    const XMFLOAT3 characterForward{
        std::sin(characterYaw_), 0.0f, std::cos(characterYaw_)};
    XMFLOAT3 bladeDir =
        Sub(characterForward, Scale(armDir, Dot(characterForward, armDir)));
    bladeDir = NormalizeOr(bladeDir, Cross({0.0f, 1.0f, 0.0f}, armDir));
    bladeDir = NormalizeOr(bladeDir, {0.0f, 0.0f, 1.0f});
    const XMFLOAT3 guardDir =
        NormalizeOr(Cross(bladeDir, armDir), {1.0f, 0.0f, 0.0f});
    const XMFLOAT3 guardDepthDir =
        NormalizeOr(Cross(guardDir, bladeDir), armDir);

    const XMFLOAT4 bladeRotation = AlignYAxisTo(bladeDir);
    const XMFLOAT4 guardRotation =
        QuaternionFromAxes(guardDir, bladeDir, guardDepthDir);
    const XMFLOAT3 gripCenter = Add(rightHandWorld_, Scale(armDir, 0.035f));
    const XMFLOAT3 guardCenter =
        Add(gripCenter, Scale(bladeDir, kWeaponHandleLength * 0.5f));
    const float halfGuardThickness = kWeaponGuardThickness * 0.5f;

    Transform handle{};
    handle.position =
        Add(gripCenter, Scale(bladeDir, -kWeaponHandleLength * 0.5f));
    handle.rotation = bladeRotation;
    handle.scale = {1.0f, 1.0f, 1.0f};

    Transform guard{};
    guard.position =
        Add(guardCenter, Scale(bladeDir, -halfGuardThickness));
    guard.rotation = guardRotation;
    guard.scale = {1.0f, 1.0f, 1.0f};

    Transform blade{};
    blade.position = Add(
        guardCenter, Scale(bladeDir, halfGuardThickness - kWeaponJointOverlap));
    blade.rotation = bladeRotation;
    blade.scale = {1.0f, 1.0f, 1.0f};

    ctx_->rendering.model->Draw(weaponHandleModelId_, handle, camera_);
    ctx_->rendering.model->Draw(weaponGuardModelId_, guard, camera_);
    ctx_->rendering.model->Draw(weaponBladeModelId_, blade, camera_);
}

void GameScene::DrawSceneProps() {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr) {
        return;
    }

    if (groundModelId_ != kInvalidResourceId) {
        Transform ground{};
        ground.position = {0.0f, -0.04f, 0.0f};
        ground.scale = {1.0f, 1.0f, 1.0f};
        ctx_->rendering.model->Draw(groundModelId_, ground, camera_);
    }

    if (brainStemModelId_ != kInvalidResourceId) {
        Transform brainStem{};
        brainStem.position = {1.65f, 0.0f, 0.15f};
        brainStem.scale = {0.75f, 0.75f, 0.75f};
        ctx_->rendering.model->Draw(brainStemModelId_, brainStem, camera_);
    }
}

void GameScene::DrawBoneDebugOverlay() {
#ifdef _DEBUG
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        humanModelId_ == kInvalidResourceId) {
        return;
    }

    const Model *model = ctx_->rendering.model->GetModel(humanModelId_);
    if (model == nullptr) {
        return;
    }

    if (ImGui::GetForegroundDrawList() == nullptr) {
        return;
    }

    if (debugBindPoseEnabled_) {
        DrawBindPoseBoneOverlay(*model);
    }

    if (debugRigEnabled_) {
        DrawAnimatedBoneOverlay(*model);
    }
#endif
}

void GameScene::DrawBindPoseBoneOverlay(const Model& model) {
#ifdef _DEBUG
    for (uint32_t i = 0; i < static_cast<uint32_t>(model.bones.size()); ++i) {
        const BoneInfo& bone = model.bones[i];
        if ((debugMajorBonesOnly_ && !IsMajorDebugBone(bone.name) &&
             static_cast<int>(i) != selectedBoneIndex_) ||
            bone.parentIndex < 0 || bone.parentIndex >= static_cast<int>(model.bones.size())) {
            continue;
        }
        DrawScreenBoneLine(BindBoneWorldPosition(static_cast<uint32_t>(bone.parentIndex)),
                           BindBoneWorldPosition(i), IM_COL32(255, 220, 40, 120), 1.5f);
    }
#else
    (void)model;
#endif
}

void GameScene::DrawAnimatedBoneOverlay(const Model& model) {
#ifdef _DEBUG
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    for (uint32_t i = 0; i < static_cast<uint32_t>(model.bones.size()); ++i) {
        const BoneInfo& bone = model.bones[i];
        const bool selected = static_cast<int>(i) == selectedBoneIndex_;
        if (debugMajorBonesOnly_ && !IsMajorDebugBone(bone.name) && !selected) {
            continue;
        }

        const XMFLOAT3 child = BoneWorldPosition(i);
        XMFLOAT2 jointScreen{};
        if (ProjectWorldToScreen(child, jointScreen)) {
            drawList->AddCircleFilled(ImVec2(jointScreen.x, jointScreen.y),
                                      selected ? 5.5f : 3.0f,
                                      selected ? IM_COL32(255, 228, 40, 255)
                                               : IM_COL32(90, 255, 120, 230));
        }
        if (bone.parentIndex < 0 || bone.parentIndex >= static_cast<int>(model.bones.size())) {
            continue;
        }

        const ImU32 color = bone.name.find("Left") != std::string::npos
                                ? IM_COL32(70, 135, 255, 235)
                            : bone.name.find("Right") != std::string::npos
                                ? IM_COL32(255, 80, 65, 235)
                                : IM_COL32(225, 230, 238, 225);
        DrawScreenBoneLine(BoneWorldPosition(static_cast<uint32_t>(bone.parentIndex)), child,
                           color, selected ? 4.0f : 2.5f);
    }
    DrawSelectedBoneAxes(model);
#else
    (void)model;
#endif
}

void GameScene::DrawSelectedBoneAxes(const Model& model) {
#ifdef _DEBUG
    if (!debugLocalAxesEnabled_ || selectedBoneIndex_ < 0 ||
        selectedBoneIndex_ >= static_cast<int>(model.bones.size())) {
        return;
    }

    constexpr float kAxisLength = 0.20f;
    const XMMATRIX world = BoneWorldMatrix(static_cast<uint32_t>(selectedBoneIndex_));
    const XMVECTOR origin = XMVector3TransformCoord(XMVectorZero(), world);
    const XMVECTOR localAxes[] = {
        XMVectorSet(kAxisLength, 0.0f, 0.0f, 0.0f),
        XMVectorSet(0.0f, kAxisLength, 0.0f, 0.0f),
        XMVectorSet(0.0f, 0.0f, kAxisLength, 0.0f),
    };
    constexpr ImU32 kColors[] = {
        IM_COL32(255, 70, 70, 255),
        IM_COL32(70, 255, 100, 255),
        IM_COL32(80, 145, 255, 255),
    };

    XMFLOAT3 start{};
    XMStoreFloat3(&start, origin);
    for (size_t i = 0; i < std::size(localAxes); ++i) {
        XMFLOAT3 end{};
        XMStoreFloat3(&end, XMVector3TransformCoord(localAxes[i], world));
        DrawScreenBoneLine(start, end, kColors[i], 3.0f);
    }
#else
    (void)model;
#endif
}

void GameScene::DrawDebugPanel() {
#ifdef _DEBUG
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr) {
        return;
    }

    const Model *model = ctx_->rendering.model->GetModel(humanModelId_);
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 430.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Bone Debug")) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Skeleton", &debugRigEnabled_);
    ImGui::Checkbox("Labels", &debugLabelsEnabled_);
    ImGui::SameLine();
    ImGui::Checkbox("Local axes", &debugLocalAxesEnabled_);
    ImGui::Checkbox("Bind pose", &debugBindPoseEnabled_);
    ImGui::SameLine();
    ImGui::Checkbox("Major only", &debugMajorBonesOnly_);

    ImGui::Separator();
    ImGui::Text("Sneak blend: %.2f", sneakBlend_);
    float manualBlend = manualSneakBlend_ >= 0.0f ? manualSneakBlend_ : sneakBlend_;
    if (ImGui::SliderFloat("Manual blend", &manualBlend, 0.0f, 1.0f)) {
        manualSneakBlend_ = manualBlend;
    }
    ImGui::SameLine();
    if (ImGui::Button("Auto")) {
        manualSneakBlend_ = -1.0f;
    }

    ImGui::Separator();
    ImGui::TextUnformatted(
        "Keys: Shift Sneak / F1 Rig / F3 Name / F5 Bind / F6 Filter");
    ImGui::TextUnformatted("Keys: [ ] Select bone");

    if (model == nullptr || model->bones.empty()) {
        ImGui::TextUnformatted("No bones loaded.");
        ImGui::End();
        return;
    }

    selectedBoneIndex_ =
        std::clamp(selectedBoneIndex_, 0, static_cast<int>(model->bones.size()) - 1);
    const BoneInfo &selected = model->bones[static_cast<size_t>(selectedBoneIndex_)];
    const std::string selectedName = DisplayBoneName(selected.name);

    if (ImGui::BeginCombo("Selected", selectedName.c_str())) {
        for (int i = 0; i < static_cast<int>(model->bones.size()); ++i) {
            const std::string name = DisplayBoneName(model->bones[static_cast<size_t>(i)].name);
            const bool isSelected = selectedBoneIndex_ == i;
            if (ImGui::Selectable(name.c_str(), isSelected)) {
                selectedBoneIndex_ = i;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    const XMFLOAT3 position =
        BoneWorldPosition(static_cast<uint32_t>(selectedBoneIndex_));
    ImGui::Text("Index: %d / %d", selectedBoneIndex_,
                static_cast<int>(model->bones.size()) - 1);
    ImGui::Text("Parent: %d", selected.parentIndex);
    ImGui::Text("World: %.3f, %.3f, %.3f", position.x, position.y, position.z);
    ImGui::Text("Children:");
    ImGui::Indent();
    bool hasChild = false;
    for (int i = 0; i < static_cast<int>(model->bones.size()); ++i) {
        if (model->bones[static_cast<size_t>(i)].parentIndex == selectedBoneIndex_) {
            hasChild = true;
            ImGui::TextUnformatted(
                DisplayBoneName(model->bones[static_cast<size_t>(i)].name).c_str());
        }
    }
    if (!hasChild) {
        ImGui::TextUnformatted("(none)");
    }
    ImGui::Unindent();

    ImGui::End();
#endif
}

bool GameScene::DrawParticleEmitterControls(const char* label,
                                            ParticleEmitterSettings& settings,
                                            bool& enabled) {
#ifdef _DEBUG
    if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
        return false;
    }

    ImGui::PushID(label);
    bool changed = ImGui::Checkbox("Enabled", &enabled);
    constexpr const char* kShapeNames[] = {
        "Point", "Sphere", "Box", "Ring", "Disk", "Arc", "Triangle", "Mesh surface"};
    int shape = static_cast<int>(settings.spawnShape);
    if (ImGui::Combo("Shape", &shape, kShapeNames, IM_ARRAYSIZE(kShapeNames))) {
        settings.spawnShape = static_cast<ParticleSpawnShape>(shape);
        changed = true;
    }

    int burstCount = static_cast<int>(settings.burstCount);
    if (ImGui::DragInt("Particles / emission", &burstCount, 1.0f, 1, 256)) {
        settings.burstCount = static_cast<uint32_t>(burstCount);
        changed = true;
    }
    int particlesPerThread = static_cast<int>(settings.particlesPerThread);
    if (ImGui::SliderInt("Particles / thread", &particlesPerThread, 1, 8)) {
        settings.particlesPerThread = static_cast<uint32_t>(particlesPerThread);
        changed = true;
    }

    changed |= ImGui::DragFloat("Emission rate", &settings.emitRate, 0.5f, 0.1f, 120.0f);
    changed |= ImGui::DragFloat3("Spawn size", &settings.spawnOffsetScale.x, 0.01f, 0.0f, 2.0f);
    changed |= ImGui::ColorEdit4("Color", &settings.tintColor.x);
    changed |= ImGui::DragFloat("Lifetime", &settings.baseLifeTime, 0.01f, 0.05f, 8.0f);
    changed |= ImGui::DragFloat("Radial speed", &settings.radialVelocity, 0.01f, 0.0f, 8.0f);
    changed |= ImGui::DragFloat("Directional speed", &settings.directionalVelocity,
                                0.01f, 0.0f, 8.0f);
    changed |= ImGui::DragFloat("Turbulence", &settings.turbulence, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::DragFloat("Stretch", &settings.stretch, 0.05f, 0.0f, 12.0f);
    changed |= ImGui::SliderFloat("Light influence", &settings.lightInfluence, 0.0f, 1.0f);
    constexpr const char* kLightNames[] = {"None", "Right hand", "Left hand"};
    int lightSelection = settings.assignedLight < 2u
                             ? static_cast<int>(settings.assignedLight) + 1
                             : 0;
    if (ImGui::Combo("Assigned light", &lightSelection, kLightNames,
                     IM_ARRAYSIZE(kLightNames))) {
        settings.assignedLight = lightSelection > 0
                                     ? static_cast<uint32_t>(lightSelection - 1)
                                     : UINT32_MAX;
        changed = true;
    }
    ImGui::PopID();
    return changed;
#else
    (void)label;
    (void)settings;
    (void)enabled;
    return false;
#endif
}

bool GameScene::DrawParticleFieldControls() {
#ifdef _DEBUG
    if (!ImGui::CollapsingHeader("Fields", ImGuiTreeNodeFlags_DefaultOpen)) {
        return false;
    }

    constexpr const char* kFieldNames[] = {"Directional", "Radial", "Vortex", "Drag"};
    bool changed = false;
    for (size_t i = 0; i < particleFields_.size(); ++i) {
        ParticleFieldSettings& field = particleFields_[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::SeparatorText(kFieldNames[static_cast<size_t>(field.type)]);
        changed |= ImGui::Checkbox("Enabled", &field.enabled);
        int type = static_cast<int>(field.type);
        if (ImGui::Combo("Type", &type, kFieldNames, IM_ARRAYSIZE(kFieldNames))) {
            field.type = static_cast<ParticleFieldType>(type);
            changed = true;
        }
        changed |= ImGui::DragFloat("Radius", &field.radius, 0.05f, 0.05f, 10.0f);
        changed |= ImGui::DragFloat("Strength", &field.strength, 0.05f, -10.0f, 10.0f);
        changed |= ImGui::DragFloat("Falloff", &field.falloff, 0.05f, 0.05f, 8.0f);
        changed |= ImGui::DragFloat3("Direction", &field.direction.x, 0.02f, -1.0f, 1.0f);
        ImGui::PopID();
    }
    return changed;
#else
    return false;
#endif
}

void GameScene::DrawParticleEditor() {
#ifdef _DEBUG
    if (!handParticlesReady_) {
        return;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2((std::max)(16.0f, displaySize.x - 396.0f), 16.0f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380.0f, (std::min)(720.0f, displaySize.y - 32.0f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("GPU Particle Editor")) {
        ImGui::End();
        return;
    }

    ImGui::Text("GPU threads: 256 | Emitters: %zu | Fields: %zu",
                handParticles_.GetEmitterCount(), particleFields_.size());
    ImGui::TextUnformatted(
        "Point / Sphere / Box / Ring / Disk / Arc / Triangle / Mesh surface");
    if (ImGui::Button("Clear particles")) {
        handParticles_.Clear();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Trail", &particleTrailEnabled_)) {
        particleMaterial_.params1.x = particleTrailEnabled_ ? 4.0f : 0.0f;
        handParticles_.SetMaterialSettings(particleMaterial_);
    }

    if (DrawParticleEmitterControls("Right Hand Emitter", rightParticleEmitter_,
                                    rightParticleEmitterEnabled_)) {
        handParticles_.UpdateEmitter(rightParticleEmitterId_, rightParticleEmitter_);
        handParticles_.SetEmitterEnabled(rightParticleEmitterId_, rightParticleEmitterEnabled_);
    }
    if (DrawParticleEmitterControls("Left Hand Emitter", leftParticleEmitter_,
                                    leftParticleEmitterEnabled_)) {
        handParticles_.UpdateEmitter(leftParticleEmitterId_, leftParticleEmitter_);
        handParticles_.SetEmitterEnabled(leftParticleEmitterId_, leftParticleEmitterEnabled_);
    }

    if (DrawParticleFieldControls()) {
        handParticles_.SetFields(particleFields_);
    }

    if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool lightingChanged = false;
        lightingChanged |= ImGui::DragFloat3("Light direction", &particleLighting_.direction.x,
                                             0.02f, -1.0f, 1.0f);
        lightingChanged |=
            ImGui::DragFloat("Light intensity", &particleLighting_.intensity, 0.05f, 0.0f, 8.0f);
        lightingChanged |= ImGui::ColorEdit3("Light color", &particleLighting_.color.x);
        lightingChanged |= ImGui::ColorEdit3("Ambient", &particleLighting_.ambient.x);
        constexpr const char* kAssignedLightNames[] = {"Right hand light", "Left hand light"};
        for (size_t i = 0; i < particleLighting_.pointLightCount; ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::SeparatorText(kAssignedLightNames[i]);
            lightingChanged |= ImGui::ColorEdit3("Color", &particleLighting_.pointLights[i].color.x);
            lightingChanged |= ImGui::DragFloat("Intensity",
                                                &particleLighting_.pointLights[i].intensity,
                                                0.05f, 0.0f, 8.0f);
            lightingChanged |= ImGui::DragFloat("Range", &particleLighting_.pointLights[i].range,
                                                0.05f, 0.05f, 8.0f);
            ImGui::PopID();
        }
        if (lightingChanged) {
            handParticles_.SetLightingSettings(particleLighting_);
        }
    }

    ImGui::End();
#endif
}

void GameScene::DrawBoneLabels() {
    if (!debugLabelsEnabled_ || ctx_ == nullptr || ctx_->rendering.model == nullptr) {
        return;
    }

#ifdef _DEBUG
    const Model *model = ctx_->rendering.model->GetModel(humanModelId_);
    if (model == nullptr) {
        return;
    }

    if (ImGui::GetForegroundDrawList() == nullptr) {
        return;
    }

    const int hoveredBoneIndex = FindHoveredBone(*model);
    if (hoveredBoneIndex >= 0 && hoveredBoneIndex != selectedBoneIndex_) {
        DrawBoneLabel(*model, static_cast<uint32_t>(hoveredBoneIndex), false);
    }
    if (selectedBoneIndex_ >= 0 &&
        selectedBoneIndex_ < static_cast<int>(model->bones.size())) {
        DrawBoneLabel(*model, static_cast<uint32_t>(selectedBoneIndex_), true);
    }
#endif
}

int GameScene::FindHoveredBone(const Model& model) const {
#ifdef _DEBUG
    if (ImGui::GetIO().WantCaptureMouse) {
        return -1;
    }

    constexpr float kHoverRadius = 18.0f;
    const ImVec2 mouse = ImGui::GetMousePos();
    float nearestDistanceSquared = kHoverRadius * kHoverRadius;
    int nearestBoneIndex = -1;
    for (uint32_t i = 0; i < static_cast<uint32_t>(model.bones.size()); ++i) {
        XMFLOAT2 screen{};
        if (!ProjectWorldToScreen(BoneWorldPosition(i), screen)) {
            continue;
        }

        const float deltaX = screen.x - mouse.x;
        const float deltaY = screen.y - mouse.y;
        const float distanceSquared = deltaX * deltaX + deltaY * deltaY;
        if (distanceSquared < nearestDistanceSquared) {
            nearestDistanceSquared = distanceSquared;
            nearestBoneIndex = static_cast<int>(i);
        }
    }
    return nearestBoneIndex;
#else
    (void)model;
    return -1;
#endif
}

void GameScene::DrawBoneLabel(const Model& model, uint32_t boneIndex, bool selected) {
#ifdef _DEBUG
    if (boneIndex >= model.bones.size()) {
        return;
    }

    XMFLOAT2 screen{};
    if (!ProjectWorldToScreen(BoneWorldPosition(boneIndex), screen)) {
        return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const std::string label = DisplayBoneName(model.bones[boneIndex].name);
    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    const ImVec2 jointPosition(screen.x, screen.y);
    const ImVec2 textPosition = selected
                                    ? ImVec2(screen.x + 12.0f, screen.y - textSize.y - 12.0f)
                                    : ImVec2(screen.x + 12.0f, screen.y + 10.0f);
    constexpr float kPadding = 4.0f;
    const ImVec2 backgroundMin(textPosition.x - kPadding, textPosition.y - kPadding);
    const ImVec2 backgroundMax(textPosition.x + textSize.x + kPadding,
                               textPosition.y + textSize.y + kPadding);
    const ImU32 accentColor = selected ? IM_COL32(255, 230, 45, 255)
                                       : IM_COL32(100, 220, 255, 255);

    drawList->AddLine(jointPosition,
                      ImVec2(backgroundMin.x, selected ? backgroundMax.y : backgroundMin.y),
                      accentColor, selected ? 2.0f : 1.5f);
    drawList->AddCircleFilled(jointPosition, selected ? 4.5f : 3.5f, accentColor);
    drawList->AddRectFilled(backgroundMin, backgroundMax, IM_COL32(10, 12, 18, 220), 3.0f);
    drawList->AddRect(backgroundMin, backgroundMax, accentColor, 3.0f, 0, 1.0f);
    drawList->AddText(textPosition, IM_COL32(245, 248, 255, 255), label.c_str());
#else
    (void)model;
    (void)boneIndex;
    (void)selected;
#endif
}

uint32_t
GameScene::FindBoneIndex(const std::vector<std::string> &candidates) const {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        humanModelId_ == kInvalidResourceId) {
        return kInvalidResourceId;
    }

    const Model *model = ctx_->rendering.model->GetModel(humanModelId_);
    if (model == nullptr) {
        return kInvalidResourceId;
    }

    const auto exactMatch = std::ranges::find_if(candidates, [model](const std::string& name) {
        return model->boneMap.contains(name);
    });
    if (exactMatch != candidates.end()) {
        return model->boneMap.at(*exactMatch);
    }

    for (uint32_t i = 0; i < static_cast<uint32_t>(model->bones.size()); ++i) {
        const bool containsCandidate =
            std::ranges::any_of(candidates, [model, i](const std::string& name) {
                return model->bones[i].name.find(name) != std::string::npos;
            });
        if (containsCandidate) {
            return i;
        }
    }
    return kInvalidResourceId;
}

XMFLOAT3 GameScene::BoneWorldPosition(uint32_t boneIndex) const {
    const XMVECTOR position = XMVector3TransformCoord(XMVectorZero(), BoneWorldMatrix(boneIndex));

    XMFLOAT3 result{};
    XMStoreFloat3(&result, position);
    return result;
}

XMMATRIX GameScene::BoneWorldMatrix(uint32_t boneIndex) const {
    XMMATRIX world = CharacterWorldMatrix();
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        humanModelId_ == kInvalidResourceId) {
        return world;
    }

    const Model* model = ctx_->rendering.model->GetModel(humanModelId_);
    if (model == nullptr || boneIndex == kInvalidResourceId ||
        boneIndex >= model->skeletonSpaceMatrices.size()) {
        return world;
    }
    if (model->hasRootAnimation) {
        world = XMLoadFloat4x4(&model->rootAnimationMatrix) * world;
    }
    return XMLoadFloat4x4(&model->skeletonSpaceMatrices[boneIndex]) * world;
}

XMFLOAT3 GameScene::BindBoneWorldPosition(uint32_t boneIndex) const {
    if (boneIndex == kInvalidResourceId || boneIndex >= bindPoseMatrices_.size()) {
        return characterTransform_.position;
    }

    XMMATRIX bone = XMLoadFloat4x4(&bindPoseMatrices_[boneIndex]);
    XMVECTOR position =
        XMVector3TransformCoord(XMVectorZero(), bone * CharacterWorldMatrix());

    XMFLOAT3 result{};
    XMStoreFloat3(&result, position);
    return result;
}

XMMATRIX GameScene::CharacterWorldMatrix() const {
    return XMMatrixScaling(characterTransform_.scale.x, characterTransform_.scale.y,
                           characterTransform_.scale.z) *
           XMMatrixRotationQuaternion(XMLoadFloat4(&characterTransform_.rotation)) *
           XMMatrixTranslation(characterTransform_.position.x,
                               characterTransform_.position.y,
                               characterTransform_.position.z);
}

bool GameScene::IsMajorDebugBone(const std::string &boneName) {
    return ContainsAny(boneName,
                       {"Root", "Hips", "Spine", "Neck", "Head", "Shoulder",
                        "Arm", "ForeArm", "Hand", "UpLeg", "Leg", "Foot",
                        "Toe"});
}

std::string GameScene::DisplayBoneName(const std::string &boneName) {
    constexpr const char *prefix = "mixamorig:";
    if (boneName.rfind(prefix, 0) == 0) {
        return boneName.substr(std::char_traits<char>::length(prefix));
    }
    return boneName;
}

bool GameScene::ProjectWorldToScreen(const XMFLOAT3 &world,
                                           XMFLOAT2 &screen) const {
    if (ctx_ == nullptr || ctx_->systems.winApp == nullptr) {
        return false;
    }

    const float width = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float height = static_cast<float>(ctx_->systems.winApp->GetHeight());
    if (width <= 0.0f || height <= 0.0f) {
        return false;
    }

    XMVECTOR clip =
        XMVector3TransformCoord(XMLoadFloat3(&world), camera_.GetViewProjection());
    const float x = XMVectorGetX(clip);
    const float y = XMVectorGetY(clip);
    const float z = XMVectorGetZ(clip);
    if (z < 0.0f || z > 1.0f || std::abs(x) > 1.3f || std::abs(y) > 1.3f) {
        return false;
    }

    screen.x = (x * 0.5f + 0.5f) * width;
    screen.y = (-y * 0.5f + 0.5f) * height;
    return true;
}

void GameScene::DrawScreenBoneLine(const XMFLOAT3 &a, const XMFLOAT3 &b,
                                         uint32_t color, float thickness) {
#ifdef _DEBUG
    XMFLOAT2 screenA{};
    XMFLOAT2 screenB{};
    if (!ProjectWorldToScreen(a, screenA) || !ProjectWorldToScreen(b, screenB)) {
        return;
    }

    ImDrawList *drawList = ImGui::GetForegroundDrawList();
    if (drawList == nullptr) {
        return;
    }
    drawList->AddLine(ImVec2(screenA.x, screenA.y), ImVec2(screenB.x, screenB.y),
                      color, thickness);
#else
    (void)a;
    (void)b;
    (void)color;
    (void)thickness;
#endif
}

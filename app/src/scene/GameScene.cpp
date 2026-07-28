#include "scene/GameScene.h"

#include "animation/SkeletonPoseBuilder.h"

#ifdef ENABLE_IMGUI
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
    material.enableTexture = 0;
    material.reflectionStrength = 0.0f;
    material.reflectionFresnelStrength = 0.0f;
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

bool IsKeyOrGamepadButtonTriggered(const Input& input, int dik, WORD button) {
    return input.IsKeyTrigger(dik) || input.IsGamepadButtonTrigger(button);
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

std::vector<XMMATRIX> BuildGlobalPoseMatrices(
    const Model& model, const std::vector<XMMATRIX>& localMatrices) {
    std::vector<XMMATRIX> globals(model.bones.size(), XMMatrixIdentity());
    for (size_t i = 0; i < model.bones.size(); ++i) {
        const XMMATRIX local = i < localMatrices.size()
                                   ? localMatrices[i]
                                   : XMLoadFloat4x4(&model.bones[i].localBindMatrix);
        const int parentIndex = model.bones[i].parentIndex;
        globals[i] = parentIndex >= 0 && parentIndex < static_cast<int>(i)
                         ? local * globals[static_cast<size_t>(parentIndex)]
                         : local;
    }
    return globals;
}

bool RotatePoseBoneToward(const Model& model, std::vector<XMMATRIX>& localMatrices,
                          uint32_t boneIndex, const XMVECTOR& target,
                          const XMVECTOR& localForward, float weight,
                          float maxAngleRadians) {
    if (boneIndex == kInvalidResourceId || boneIndex >= model.bones.size() ||
        boneIndex >= localMatrices.size() || weight <= 0.0001f) {
        return false;
    }

    const std::vector<XMMATRIX> globals = BuildGlobalPoseMatrices(model, localMatrices);
    XMVECTOR scale{};
    XMVECTOR rotation{};
    XMVECTOR translation{};
    if (!XMMatrixDecompose(&scale, &rotation, &translation, globals[boneIndex])) {
        return false;
    }

    const XMVECTOR desiredOffset = XMVectorSubtract(target, translation);
    if (XMVectorGetX(XMVector3LengthSq(desiredOffset)) <= 0.000001f) {
        return false;
    }
    const XMVECTOR desired = XMVector3Normalize(desiredOffset);
    const XMMATRIX currentRotation = XMMatrixRotationQuaternion(rotation);
    const XMVECTOR current =
        XMVector3Normalize(XMVector3TransformNormal(localForward, currentRotation));
    const float dot = std::clamp(XMVectorGetX(XMVector3Dot(current, desired)), -1.0f, 1.0f);
    float angle = std::acos(dot);
    if (angle <= 0.0001f) {
        return false;
    }

    XMVECTOR axis = XMVector3Cross(current, desired);
    if (XMVectorGetX(XMVector3LengthSq(axis)) <= 0.000001f) {
        axis = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    } else {
        axis = XMVector3Normalize(axis);
    }
    angle = (std::min)(angle, maxAngleRadians) * std::clamp(weight, 0.0f, 1.0f);
    const XMMATRIX adjustedGlobal =
        XMMatrixScalingFromVector(scale) * currentRotation *
        XMMatrixRotationAxis(axis, angle) * XMMatrixTranslationFromVector(translation);

    const int parentIndex = model.bones[boneIndex].parentIndex;
    if (parentIndex < 0 || parentIndex >= static_cast<int>(globals.size())) {
        localMatrices[boneIndex] = adjustedGlobal;
        return true;
    }
    const XMMATRIX parentInverse =
        XMMatrixInverse(nullptr, globals[static_cast<size_t>(parentIndex)]);
    localMatrices[boneIndex] = adjustedGlobal * parentInverse;
    return true;
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
    Material boneMaterial = MakeDebugMaterial({1.0f, 1.0f, 1.0f, 1.0f});
    Material boneCenterMaterial =
        MakeDebugMaterial({1.0f, 1.0f, 1.0f, 1.0f});
    Material boneLeftMaterial =
        MakeDebugMaterial({1.0f, 1.0f, 1.0f, 1.0f});
    Material boneRightMaterial =
        MakeDebugMaterial({1.0f, 1.0f, 1.0f, 1.0f});
    Material boneBindMaterial =
        MakeDebugMaterial({1.0f, 1.0f, 1.0f, 1.0f});
    Material boneSelectedMaterial =
        MakeDebugMaterial({1.0f, 1.0f, 1.0f, 1.0f});
    Material axisXMaterial =
        MakeDebugMaterial({1.0f, 0.20f, 0.20f, 1.0f});
    Material axisYMaterial =
        MakeDebugMaterial({0.20f, 1.0f, 0.30f, 1.0f});
    Material axisZMaterial =
        MakeDebugMaterial({0.20f, 0.45f, 1.0f, 1.0f});
    Material lookAtMaterial =
        MakeDebugMaterial({1.0f, 0.20f, 0.78f, 1.0f});

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
    boneAxisXModelId_ = ctx_->rendering.model->CreateCylinder(
        whiteTextureId_, axisXMaterial, 8u, 0.018f, 0.018f, 1.0f);
    boneAxisYModelId_ = ctx_->rendering.model->CreateCylinder(
        whiteTextureId_, axisYMaterial, 8u, 0.018f, 0.018f, 1.0f);
    boneAxisZModelId_ = ctx_->rendering.model->CreateCylinder(
        whiteTextureId_, axisZMaterial, 8u, 0.018f, 0.018f, 1.0f);
    lookAtLineModelId_ = ctx_->rendering.model->CreateCylinder(
        whiteTextureId_, lookAtMaterial, 8u, 0.012f, 0.012f, 1.0f);
    lookAtTargetModelId_ = ctx_->rendering.model->CreateSphere(
        whiteTextureId_, lookAtMaterial, 16u, 8u, 1.0f);

    characterTransform_.position = {0.0f, 0.0f, 0.0f};
    characterTransform_.scale = {1.0f, 1.0f, 1.0f};

    const Model *model = ctx_->rendering.model->GetModel(humanModelId_);
    if (model != nullptr) {
        rightHandBone_ = FindBoneIndex({"mixamorig:RightHand", "RightHand"});
        leftHandBone_ = FindBoneIndex({"mixamorig:LeftHand", "LeftHand"});
        rightFootBone_ = FindBoneIndex({"mixamorig:RightFoot", "RightFoot"});
        leftFootBone_ = FindBoneIndex({"mixamorig:LeftFoot", "LeftFoot"});
        neckBone_ = FindBoneIndex({"mixamorig:Neck", "Neck"});
        headBone_ = FindBoneIndex({"mixamorig:Head", "Head"});
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
    particleMaterial_.params1.x = particleTrailEnabled_ ? 4.0f : 0.0f;
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
    UpdateBrainStemDisplay(deltaTime);
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

    const Input* input = ctx_->systems.input;
    float moveX = input->GetGamepadLeftStickX();
    float moveZ = input->GetGamepadLeftStickY();

    if (input->IsKeyPress(DIK_A)) {
        moveX -= 1.0f;
    }
    if (input->IsKeyPress(DIK_D)) {
        moveX += 1.0f;
    }
    if (input->IsKeyPress(DIK_W)) {
        moveZ += 1.0f;
    }
    if (input->IsKeyPress(DIK_S)) {
        moveZ -= 1.0f;
    }

    float length = std::sqrt(LengthSq(moveX, moveZ));
    if (length > 1.0f) {
        moveX /= length;
        moveZ /= length;
        length = 1.0f;
    }
    walkTimeScale_ = length > 0.2f ? length : 0.0f;

    if (manualSneakBlend_ >= 0.0f) {
        sneakBlend_ = std::clamp(manualSneakBlend_, 0.0f, 1.0f);
    } else {
        const bool sneakHeld = input->IsKeyPress(DIK_LSHIFT) ||
                               input->IsKeyPress(DIK_RSHIFT) ||
                               input->GetGamepadLeftTrigger() > 0.2f;
        const float targetBlend = sneakHeld ? 1.0f : 0.0f;
        constexpr float blendSpeed = 9.0f;
        sneakBlend_ += (targetBlend - sneakBlend_) *
                       std::clamp(deltaTime * blendSpeed, 0.0f, 1.0f);
        if (std::abs(targetBlend - sneakBlend_) < 0.01f) {
            sneakBlend_ = targetBlend;
        }
    }

    if (length <= 0.2f) {
        return;
    }

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
    if (IsKeyOrGamepadButtonTriggered(*input, DIK_F1,
                                      XINPUT_GAMEPAD_DPAD_UP)) {
        debugRigEnabled_ = !debugRigEnabled_;
    }
    if (IsKeyOrGamepadButtonTriggered(*input, DIK_F3,
                                      XINPUT_GAMEPAD_DPAD_RIGHT)) {
        debugLabelsEnabled_ = !debugLabelsEnabled_;
    }
    if (IsKeyOrGamepadButtonTriggered(*input, DIK_F5,
                                      XINPUT_GAMEPAD_DPAD_DOWN)) {
        debugBindPoseEnabled_ = !debugBindPoseEnabled_;
    }
    if (IsKeyOrGamepadButtonTriggered(*input, DIK_F6,
                                      XINPUT_GAMEPAD_DPAD_LEFT)) {
        debugMajorBonesOnly_ = !debugMajorBonesOnly_;
    }
    if (IsKeyOrGamepadButtonTriggered(*input, DIK_F7, XINPUT_GAMEPAD_Y)) {
        lookAtIkEnabled_ = !lookAtIkEnabled_;
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

    if (IsKeyOrGamepadButtonTriggered(*input, DIK_RBRACKET,
                                      XINPUT_GAMEPAD_RIGHT_SHOULDER)) {
        selectedBoneIndex_ =
            (selectedBoneIndex_ + 1) % static_cast<int>(model->bones.size());
    }
    if (IsKeyOrGamepadButtonTriggered(*input, DIK_LBRACKET,
                                      XINPUT_GAMEPAD_LEFT_SHOULDER)) {
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
        ApplyHeadLookAtIk(*humanModel, walkLocals);
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

    ApplyHeadLookAtIk(*humanModel, blendedLocals);
    SkeletonPoseBuilder::UpdateSkeleton(*humanModel, blendedLocals);
    ctx_->rendering.model->GetRenderer()->UpdateSkinClusters(*humanModel);
    humanModel->animationTime = walkTime;
    humanModel->hasRootAnimation = false;
    XMStoreFloat4x4(&humanModel->rootAnimationMatrix, XMMatrixIdentity());
}

void GameScene::UpdateBrainStemDisplay(float deltaTime) {
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
        return;
    }

    brainStemDisplayTime_ =
        std::fmod(brainStemDisplayTime_ + deltaTime, 4096.0f);
    if (ctx_ != nullptr && ctx_->rendering.model != nullptr &&
        brainStemModelId_ != kInvalidResourceId) {
        ctx_->rendering.model->UpdateAnimation(brainStemModelId_,
                                               deltaTime * 0.72f);
        Model* brainStemModel =
            ctx_->rendering.model->GetModel(brainStemModelId_);
        if (brainStemModel != nullptr) {
            brainStemModel->hasRootAnimation = false;
            XMStoreFloat4x4(&brainStemModel->rootAnimationMatrix,
                            XMMatrixIdentity());
        }
    }
}

void GameScene::ApplyHeadLookAtIk(
    const Model& model, std::vector<XMMATRIX>& localMatrices) const {
    if (!lookAtIkEnabled_ || localMatrices.size() != model.bones.size() ||
        (headBone_ == kInvalidResourceId && neckBone_ == kInvalidResourceId)) {
        return;
    }

    const XMMATRIX inverseCharacter = XMMatrixInverse(nullptr, CharacterWorldMatrix());
    const XMVECTOR target =
        XMVector3TransformCoord(XMLoadFloat3(&lookAtTarget_), inverseCharacter);
    const XMVECTOR forwardAxes[] = {
        XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f),
        XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f),
        XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),
        XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f),
        XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
        XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f),
    };
    const int forwardAxis = std::clamp(lookAtForwardAxis_, 0, 5);
    const XMVECTOR forward = forwardAxes[forwardAxis];
    const float maxAngle = XMConvertToRadians(
        std::clamp(lookAtMaxAngleDegrees_, 1.0f, 120.0f));
    const float weight = std::clamp(lookAtWeight_, 0.0f, 1.0f);

    RotatePoseBoneToward(model, localMatrices, neckBone_, target, forward,
                         weight * 0.35f, maxAngle * 0.55f);
    RotatePoseBoneToward(model, localMatrices, headBone_, target, forward,
                         weight * 0.80f, maxAngle);
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

void GameScene::DrawForeground3D() {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        ctx_->rendering.lightingScene == nullptr) {
        return;
    }

    const SceneLighting sceneLighting =
        ctx_->rendering.lightingScene->GetSceneLighting();
    SceneLighting debugLighting = sceneLighting;
    debugLighting.keyLightColor = {0.30f, 0.30f, 0.30f, 1.0f};
    debugLighting.fillLightColor = {0.20f, 0.20f, 0.20f, 1.0f};
    debugLighting.ambientColor = {1.0f, 1.0f, 1.0f, 1.0f};
    debugLighting.pointLights = {};
    debugLighting.spotLight = {};
    ctx_->rendering.model->SetSceneLighting(debugLighting);

    DrawBoneDebugOverlay();
    DrawLookAtDebug();

    ctx_->rendering.model->SetSceneLighting(sceneLighting);
}

void GameScene::DrawTransparent() {
    if (handParticlesReady_) {
        handParticles_.Draw(camera_);
    }
}

void GameScene::DrawPostProcessOverlay() {
    DrawBoneLabels();
    DrawEvaluationHud();
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
        const float driftPhase = brainStemDisplayTime_ * 0.55f;
        const float hoverPhase = brainStemDisplayTime_ * 1.25f;
        brainStem.position = {
            1.65f + std::sin(driftPhase) * 0.10f,
            0.08f + std::sin(hoverPhase) * 0.06f,
            0.15f + std::cos(driftPhase) * 0.05f};

        const XMFLOAT3 cameraPosition = camera_.GetPosition();
        const float toCameraX = cameraPosition.x - brainStem.position.x;
        const float toCameraZ = cameraPosition.z - brainStem.position.z;
        const float faceCameraYaw = std::atan2(toCameraX, toCameraZ);
        const float pitch = std::sin(brainStemDisplayTime_ * 0.70f) * 0.018f;
        const float roll = std::sin(brainStemDisplayTime_ * 0.90f) * 0.025f;
        XMStoreFloat4(
            &brainStem.rotation,
            XMQuaternionRotationRollPitchYaw(pitch, faceCameraYaw, roll));

        const float pulse = 0.76f +
                            std::sin(brainStemDisplayTime_ * 1.60f) * 0.012f;
        brainStem.scale = {pulse, pulse, pulse};
        ctx_->rendering.model->Draw(brainStemModelId_, brainStem, camera_);
    }
}

void GameScene::DrawBoneDebugOverlay() {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        humanModelId_ == kInvalidResourceId) {
        return;
    }

    const Model *model = ctx_->rendering.model->GetModel(humanModelId_);
    if (model == nullptr) {
        return;
    }

    if (debugBindPoseEnabled_) {
        DrawBindPoseBoneOverlay(*model);
    }

    if (debugRigEnabled_) {
        DrawAnimatedBoneOverlay(*model);
    }
}

void GameScene::DrawBindPoseBoneOverlay(const Model& model) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(model.bones.size()); ++i) {
        const BoneInfo& bone = model.bones[i];
        if ((debugMajorBonesOnly_ && !IsMajorDebugBone(bone.name) &&
             static_cast<int>(i) != selectedBoneIndex_) ||
            bone.parentIndex < 0 || bone.parentIndex >= static_cast<int>(model.bones.size())) {
            continue;
        }
        DrawDebugSegment(
            BindBoneWorldPosition(static_cast<uint32_t>(bone.parentIndex)),
            BindBoneWorldPosition(i), boneBindModelId_);
    }
}

void GameScene::DrawAnimatedBoneOverlay(const Model& model) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(model.bones.size()); ++i) {
        const BoneInfo& bone = model.bones[i];
        const bool selected = static_cast<int>(i) == selectedBoneIndex_;
        if (debugMajorBonesOnly_ && !IsMajorDebugBone(bone.name) && !selected) {
            continue;
        }

        const XMFLOAT3 child = BoneWorldPosition(i);
        Transform joint{};
        joint.position = child;
        const float jointScale = selected ? 0.042f : 0.024f;
        joint.scale = {jointScale, jointScale, jointScale};
        const uint32_t jointModel =
            selected ? boneSelectedModelId_ : boneJointModelId_;
        if (jointModel != kInvalidResourceId) {
            ctx_->rendering.model->Draw(jointModel, joint, camera_);
        }
        if (bone.parentIndex < 0 || bone.parentIndex >= static_cast<int>(model.bones.size())) {
            continue;
        }

        const uint32_t segmentModel =
            bone.name.find("Left") != std::string::npos
                ? boneLeftModelId_
                : (bone.name.find("Right") != std::string::npos
                       ? boneRightModelId_
                       : boneCenterModelId_);
        DrawDebugSegment(
            BoneWorldPosition(static_cast<uint32_t>(bone.parentIndex)), child,
            segmentModel, selected ? 1.55f : 1.0f);
    }
    DrawSelectedBoneAxes(model);
}

void GameScene::DrawSelectedBoneAxes(const Model& model) {
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
    const uint32_t axisModels[] = {
        boneAxisXModelId_, boneAxisYModelId_, boneAxisZModelId_};

    XMFLOAT3 start{};
    XMStoreFloat3(&start, origin);
    for (size_t i = 0; i < std::size(localAxes); ++i) {
        XMFLOAT3 end{};
        XMStoreFloat3(&end, XMVector3TransformCoord(localAxes[i], world));
        DrawDebugSegment(start, end, axisModels[i], 1.35f);
    }
}

void GameScene::DrawLookAtDebug() {
    if (!debugLookAtTargetEnabled_ || !lookAtIkEnabled_ ||
        headBone_ == kInvalidResourceId) {
        return;
    }

    const XMFLOAT3 headPosition = BoneWorldPosition(headBone_);
    DrawDebugSegment(headPosition, lookAtTarget_, lookAtLineModelId_);
    if (ctx_ != nullptr && ctx_->rendering.model != nullptr &&
        lookAtTargetModelId_ != kInvalidResourceId) {
        Transform marker{};
        marker.position = lookAtTarget_;
        marker.scale = {0.075f, 0.075f, 0.075f};
        ctx_->rendering.model->Draw(lookAtTargetModelId_, marker, camera_);
    }
}

void GameScene::DrawDebugSegment(const XMFLOAT3& start, const XMFLOAT3& end,
                                 uint32_t modelId, float thicknessScale) {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        modelId == kInvalidResourceId) {
        return;
    }

    const XMFLOAT3 offset = Sub(end, start);
    const float length =
        std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
    if (!std::isfinite(length) || length <= 0.0001f) {
        return;
    }

    Transform segment{};
    segment.position = start;
    segment.rotation = AlignYAxisTo(Scale(offset, 1.0f / length));
    segment.scale = {thicknessScale, length, thicknessScale};
    ctx_->rendering.model->Draw(modelId, segment, camera_);
}

void GameScene::DrawEvaluationHud() {
    if (ctx_ == nullptr || ctx_->rendering.text == nullptr ||
        !ctx_->rendering.text->IsReady()) {
        return;
    }

    const Model* model =
        ctx_->rendering.model != nullptr
            ? ctx_->rendering.model->GetModel(humanModelId_)
            : nullptr;
    const size_t boneCount = model != nullptr ? model->bones.size() : 0u;
    const size_t meshCount = model != nullptr ? model->subMeshes.size() : 0u;

    char status[1024]{};
    std::snprintf(
        status, sizeof(status),
        "CG4 評価課題2  |  Compute Skinning / Animation Blend / GPU Particle\n"
        "WASD: 移動   Shift/LT: スニーク   F1: 骨格   F3: ボーン名   "
        "F5: バインド姿勢\n"
        "F6: 主要ボーン   F7/Y: Head LookAt IK   [ ] / LB RB: ボーン選択\n"
        "Blend %.2f   Skeleton %s   Labels %s   Bind %s   LookAt %s   "
        "GPU Particle %s\n"
        "Bones %zu   SubMeshes %zu   Emitters 2   Fields %zu   Trail %s",
        sneakBlend_, debugRigEnabled_ ? "ON" : "OFF",
        debugLabelsEnabled_ ? "ON" : "OFF",
        debugBindPoseEnabled_ ? "ON" : "OFF",
        lookAtIkEnabled_ ? "ON" : "OFF",
        handParticlesReady_ ? "ON" : "OFF", boneCount, meshCount,
        particleFields_.size(), particleTrailEnabled_ ? "ON" : "OFF");

    TextStyle shadow{};
    shadow.pixelSize = 17.0f;
    shadow.color = {0.0f, 0.0f, 0.0f, 0.88f};
    shadow.lineSpacing = 3.0f;
    ctx_->rendering.text->DrawText(status, {19.0f, 19.0f}, shadow);

    TextStyle text = shadow;
    text.color = {0.94f, 0.97f, 1.0f, 1.0f};
    ctx_->rendering.text->DrawText(status, {17.0f, 17.0f}, text);

    if (model != nullptr && selectedBoneIndex_ >= 0 &&
        selectedBoneIndex_ < static_cast<int>(model->bones.size())) {
        const BoneInfo& selected =
            model->bones[static_cast<size_t>(selectedBoneIndex_)];
        const XMFLOAT3 position =
            BoneWorldPosition(static_cast<uint32_t>(selectedBoneIndex_));
        char selectedText[256]{};
        std::snprintf(selectedText, sizeof(selectedText),
                      "Selected Bone: %s  [%d/%zu]  Parent %d  "
                      "Pos %.2f, %.2f, %.2f",
                      DisplayBoneName(selected.name).c_str(), selectedBoneIndex_,
                      model->bones.size(), selected.parentIndex, position.x,
                      position.y, position.z);

        const float screenHeight =
            ctx_->systems.winApp != nullptr
                ? static_cast<float>(ctx_->systems.winApp->GetHeight())
                : 720.0f;
        TextStyle selectedStyle = text;
        selectedStyle.pixelSize = 18.0f;
        selectedStyle.color = {1.0f, 0.88f, 0.22f, 1.0f};
        ctx_->rendering.text->DrawText(selectedText,
                                      {17.0f, screenHeight - 38.0f},
                                      selectedStyle);
    }
}

void GameScene::DrawDebugPanel() {
#ifdef ENABLE_IMGUI
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr) {
        return;
    }

    const Model* model = ctx_->rendering.model->GetModel(humanModelId_);
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440.0f, 650.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(UiText("CG4 評価パネル###CG4Evaluation",
                             "CG4 Evaluation###CG4Evaluation"))) {
        ImGui::End();
        return;
    }

    DrawEvaluationToolbar();
    if (ImGui::BeginTabBar("EvaluationTabs")) {
        const ImGuiTabItemFlags animationFlags = evaluationTabRequest_ == 0
                                                      ? ImGuiTabItemFlags_SetSelected
                                                      : ImGuiTabItemFlags_None;
        const ImGuiTabItemFlags skeletonFlags = evaluationTabRequest_ == 1
                                                     ? ImGuiTabItemFlags_SetSelected
                                                     : ImGuiTabItemFlags_None;
        const ImGuiTabItemFlags particleFlags = evaluationTabRequest_ == 2
                                                     ? ImGuiTabItemFlags_SetSelected
                                                     : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem(UiText("操作方法###ControlsTab",
                                      "Controls###ControlsTab"))) {
            DrawControlsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(UiText("アニメーション###AnimationTab",
                                      "Animation###AnimationTab"),
                                nullptr, animationFlags)) {
            DrawAnimationTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(UiText("スケルトン###SkeletonTab",
                                      "Skeleton###SkeletonTab"),
                                nullptr, skeletonFlags)) {
            DrawSkeletonTab(model);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(UiText("パーティクル###ParticleTab",
                                      "Particle###ParticleTab"),
                                nullptr, particleFlags)) {
            DrawParticleEditor();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(UiText("モデル###ModelTab", "Model###ModelTab"))) {
            DrawModelTab(model);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        evaluationTabRequest_ = -1;
    }
    ImGui::End();
#endif
}

void GameScene::DrawControlsTab() {
#ifdef ENABLE_IMGUI
    ImGui::TextWrapped(UiText(
        "起動時は骨格とLookAt注視点が表示されています。各機能はタブ内の設定または下記キーで操作できます。",
        "The skeleton and LookAt target are visible at startup. Use the tabs or the shortcuts below to control each feature."));

    ImGui::SeparatorText(UiText("キャラクター", "Character"));
    ImGui::BulletText("W / A / S / D : %s", UiText("移動", "Move"));
    ImGui::BulletText("Shift : %s",
                      UiText("スニークアニメーションへ補間",
                             "Blend to sneak animation"));

    ImGui::SeparatorText(UiText("デバッグ表示", "Debug Display"));
    ImGui::BulletText("F1 : %s", UiText("骨格表示の切り替え",
                                         "Toggle skeleton"));
    ImGui::BulletText("F3 : %s", UiText("ボーン名表示の切り替え",
                                         "Toggle bone labels"));
    ImGui::BulletText("F5 : %s", UiText("バインド姿勢表示の切り替え",
                                         "Toggle bind pose"));
    ImGui::BulletText("F6 : %s", UiText("主要ボーンのみ表示",
                                         "Show major bones only"));
    ImGui::BulletText("F7 : %s", UiText("Head LookAt IKの切り替え",
                                         "Toggle Head LookAt IK"));
    ImGui::BulletText("[ / ] : %s", UiText("前後のボーンを選択",
                                            "Select previous / next bone"));

    ImGui::SeparatorText(UiText("ゲームパッド（XInput）", "Gamepad (XInput)"));
    ImGui::BulletText("%s", UiText("左スティック: 移動（倒し方で速度変化）",
                                     "Left stick: Move (analog speed)"));
    ImGui::BulletText("LT : %s", UiText("スニークアニメーションへ補間",
                                         "Blend to sneak animation"));
    ImGui::BulletText("D-Pad Up / Right / Down / Left : %s",
                      UiText("骨格 / ボーン名 / バインド姿勢 / 主要ボーン",
                             "Skeleton / Labels / Bind pose / Major bones"));
    ImGui::BulletText("Y : %s", UiText("Head LookAt IKの切り替え",
                                        "Toggle Head LookAt IK"));
    ImGui::BulletText("LB / RB : %s", UiText("前後のボーンを選択",
                                              "Select previous / next bone"));

    ImGui::SeparatorText(UiText("ImGuiパネル", "ImGui Panel"));
    ImGui::BulletText("%s", UiText("上部のタブで評価機能を切り替えます。",
                                     "Use the tabs above to switch evaluation tools."));
    ImGui::BulletText("%s", UiText("言語 / Languageから日本語と英語を切り替えられます。",
                                     "Use Language to switch between Japanese and English."));
    ImGui::BulletText("%s", UiText("パネルは常時表示され、閉じる操作はありません。",
                                     "The panel is always visible and cannot be closed."));
#endif
}

void GameScene::DrawEvaluationToolbar() {
#ifdef ENABLE_IMGUI
    ImGui::TextUnformatted(UiText("表示プリセット:", "Presets:"));
    ImGui::SameLine();
    const char* presetNames[] = {
        UiText("通常", "Clean"), UiText("骨格", "Skeleton"),
        "LookAt", UiText("パーティクル", "Particle")};
    for (uint32_t i = 0; i < static_cast<uint32_t>(std::size(presetNames)); ++i) {
        if (i > 0u) {
            ImGui::SameLine();
        }
        if (ImGui::Button(presetNames[i])) {
            ApplyEvaluationPreset(i);
        }
    }
    const char* languageNames[] = {"日本語", "English"};
    int language = japaneseUiEnabled_ ? 0 : 1;
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::Combo("言語 / Language###UiLanguage", &language, languageNames,
                     IM_ARRAYSIZE(languageNames))) {
        japaneseUiEnabled_ = language == 0;
    }
    ImGui::Separator();
#endif
}

void GameScene::DrawAnimationTab() {
#ifdef ENABLE_IMGUI
    ImGui::SeparatorText(UiText("アニメーション補間", "Animation Blend"));
    ImGui::Text(UiText("スニーク補間: %.2f", "Sneak blend: %.2f"), sneakBlend_);
    float manualBlend = manualSneakBlend_ >= 0.0f ? manualSneakBlend_ : sneakBlend_;
    if (ImGui::SliderFloat(UiText("手動補間###ManualBlend", "Manual blend###ManualBlend"),
                           &manualBlend, 0.0f, 1.0f)) {
        manualSneakBlend_ = manualBlend;
    }
    ImGui::SameLine();
    if (ImGui::Button(UiText("自動###AutoBlend", "Auto###AutoBlend"))) {
        manualSneakBlend_ = -1.0f;
    }

    ImGui::SeparatorText(UiText("頭部 LookAt IK", "Head LookAt IK"));
    ImGui::Checkbox(UiText("有効##LookAt", "Enabled##LookAt"), &lookAtIkEnabled_);
    ImGui::SameLine();
    ImGui::Checkbox(UiText("ターゲット表示###TargetDebug",
                           "Target debug###TargetDebug"),
                    &debugLookAtTargetEnabled_);
    ImGui::DragFloat3(UiText("注視点###LookAtTarget", "Target###LookAtTarget"),
                      &lookAtTarget_.x, 0.02f, -8.0f, 8.0f);
    ImGui::SliderFloat(UiText("適用率###LookAtWeight", "Weight###LookAtWeight"),
                       &lookAtWeight_, 0.0f, 1.0f);
    ImGui::SliderFloat(UiText("最大角度###LookAtAngle", "Max angle###LookAtAngle"),
                       &lookAtMaxAngleDegrees_, 1.0f, 120.0f, "%.0f deg");
    constexpr const char* kForwardAxes[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    ImGui::Combo(UiText("頭の前方向###HeadForward", "Head forward###HeadForward"),
                 &lookAtForwardAxis_, kForwardAxes,
                 IM_ARRAYSIZE(kForwardAxes));
    ImGui::Spacing();
    ImGui::TextDisabled(UiText("Shift: スニーク | F7: LookAt 切替",
                               "Shift: Sneak | F7: LookAt ON/OFF"));
#endif
}

void GameScene::DrawSkeletonTab(const Model* model) {
#ifdef ENABLE_IMGUI
    ImGui::Checkbox(UiText("骨格###Skeleton", "Skeleton###Skeleton"), &debugRigEnabled_);
    ImGui::SameLine();
    ImGui::Checkbox(UiText("ボーン名###Labels", "Labels###Labels"), &debugLabelsEnabled_);
    ImGui::Checkbox(UiText("ローカル軸###LocalAxes", "Local axes###LocalAxes"),
                    &debugLocalAxesEnabled_);
    ImGui::SameLine();
    ImGui::Checkbox(UiText("バインド姿勢###BindPose", "Bind pose###BindPose"),
                    &debugBindPoseEnabled_);
    ImGui::SameLine();
    ImGui::Checkbox(UiText("主要のみ###MajorOnly", "Major only###MajorOnly"),
                    &debugMajorBonesOnly_);
    ImGui::TextDisabled(UiText(
        "F1: 骨格 | F3: 名前 | F5: バインド | F6: 絞込 | [ ]: 選択",
        "F1: Rig | F3: Labels | F5: Bind | F6: Filter | [ ]: Select"));
    ImGui::SeparatorText(UiText("ボーン情報", "Bone Inspector"));
    if (model == nullptr || model->bones.empty()) {
        ImGui::TextUnformatted(UiText("ボーンが読み込まれていません。",
                                      "No bones loaded."));
        return;
    }

    selectedBoneIndex_ =
        std::clamp(selectedBoneIndex_, 0, static_cast<int>(model->bones.size()) - 1);
    const BoneInfo &selected = model->bones[static_cast<size_t>(selectedBoneIndex_)];
    const std::string selectedName = DisplayBoneName(selected.name);

    if (ImGui::BeginCombo(UiText("選択中###SelectedBone", "Selected###SelectedBone"),
                          selectedName.c_str())) {
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
    ImGui::Text(UiText("番号: %d / %d", "Index: %d / %d"), selectedBoneIndex_,
                static_cast<int>(model->bones.size()) - 1);
    ImGui::Text(UiText("親: %d", "Parent: %d"), selected.parentIndex);
    ImGui::Text(UiText("ワールド座標: %.3f, %.3f, %.3f",
                       "World: %.3f, %.3f, %.3f"),
                position.x, position.y, position.z);
    ImGui::TextUnformatted(UiText("子ボーン:", "Children:"));
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
        ImGui::TextUnformatted(UiText("（なし）", "(none)"));
    }
    ImGui::Unindent();
#else
    (void)model;
#endif
}

void GameScene::DrawModelTab(const Model* model) const {
#ifdef ENABLE_IMGUI
    ImGui::SeparatorText(UiText("読込済みアセット", "Loaded Assets"));
    ImGui::BulletText(UiText("キャラクター: %s", "Human: %s"),
                      model != nullptr ? UiText("準備完了", "Ready")
                                       : UiText("未読込", "Missing"));
    ImGui::BulletText("BrainStem: %s",
                      brainStemModelId_ != kInvalidResourceId
                          ? UiText("準備完了", "Ready")
                          : UiText("未読込", "Missing"));
    if (model != nullptr) {
        ImGui::Text(UiText("ボーン数: %zu", "Bones: %zu"), model->bones.size());
        ImGui::Text(UiText("サブメッシュ数: %zu", "SubMeshes: %zu"),
                    model->subMeshes.size());
        ImGui::Text(UiText("アニメーション数: %zu", "Animations: %zu"),
                    model->animations.size());
    }

    ImGui::SeparatorText(UiText("評価項目", "Evaluation Checklist"));
    ImGui::BulletText(UiText("Compute Shader スキニング", "Compute Shader Skinning"));
    ImGui::BulletText(UiText("マルチメッシュ / マルチマテリアル",
                             "MultiMesh / MultiMaterial"));
    ImGui::BulletText(UiText("アニメーション補間 + LookAt IK",
                             "Animation Blend + LookAt IK"));
    ImGui::BulletText(UiText("骨格 / ボーン名 / ローカル軸",
                             "Skeleton / Labels / Local Axes"));
    ImGui::BulletText(UiText("ボーンへのオブジェクト追従", "Bone Attachments"));
    ImGui::BulletText(UiText("GPUパーティクル / フィールド / メッシュ / ライト",
                             "GPU Particle / Fields / Mesh / Lights"));
#else
    (void)model;
#endif
}

void GameScene::ApplyEvaluationPreset(uint32_t presetIndex) {
    debugRigEnabled_ = presetIndex == 1u || presetIndex == 2u;
    debugLabelsEnabled_ = presetIndex == 1u;
    debugLocalAxesEnabled_ = presetIndex == 1u || presetIndex == 2u;
    debugBindPoseEnabled_ = false;
    debugMajorBonesOnly_ = presetIndex == 1u;
    debugLookAtTargetEnabled_ = presetIndex == 2u;
    if (presetIndex == 1u) {
        evaluationTabRequest_ = 1;
    } else if (presetIndex == 2u) {
        evaluationTabRequest_ = 0;
    } else if (presetIndex == 3u) {
        evaluationTabRequest_ = 2;
    }
    if (presetIndex == 2u) {
        lookAtIkEnabled_ = true;
        if (headBone_ != kInvalidResourceId) {
            selectedBoneIndex_ = static_cast<int>(headBone_);
        }
    }
    if (presetIndex == 3u) {
        rightParticleEmitterEnabled_ = true;
        leftParticleEmitterEnabled_ = true;
        handParticles_.SetEmitterEnabled(rightParticleEmitterId_, true);
        handParticles_.SetEmitterEnabled(leftParticleEmitterId_, true);
    }
}

const char* GameScene::UiText(const char* japanese, const char* english) const {
    return japaneseUiEnabled_ ? japanese : english;
}

bool GameScene::DrawParticleEmitterControls(const char* label,
                                            ParticleEmitterSettings& settings,
                                            bool& enabled) {
#ifdef ENABLE_IMGUI
    if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
        return false;
    }

    ImGui::PushID(label);
    bool changed = ImGui::Checkbox(UiText("有効###EmitterEnabled",
                                          "Enabled###EmitterEnabled"),
                                   &enabled);
    constexpr const char* kShapeNamesEnglish[] = {
        "Point", "Sphere", "Box", "Ring", "Disk", "Arc", "Triangle", "Mesh surface"};
    constexpr const char* kShapeNamesJapanese[] = {
        "点", "球", "箱", "リング", "円盤", "円弧", "三角形", "メッシュ表面"};
    const char* const* shapeNames =
        japaneseUiEnabled_ ? kShapeNamesJapanese : kShapeNamesEnglish;
    int shape = static_cast<int>(settings.spawnShape);
    if (ImGui::Combo(UiText("発生形状###EmitterShape", "Shape###EmitterShape"),
                     &shape, shapeNames, IM_ARRAYSIZE(kShapeNamesEnglish))) {
        settings.spawnShape = static_cast<ParticleSpawnShape>(shape);
        changed = true;
    }

    int burstCount = static_cast<int>(settings.burstCount);
    if (ImGui::DragInt(UiText("1回の発生数###EmissionCount",
                              "Particles / emission###EmissionCount"),
                       &burstCount, 1.0f, 1, 256)) {
        settings.burstCount = static_cast<uint32_t>(burstCount);
        changed = true;
    }
    int particlesPerThread = static_cast<int>(settings.particlesPerThread);
    if (ImGui::SliderInt(UiText("1スレッドの粒子数###ParticlesPerThread",
                                "Particles / thread###ParticlesPerThread"),
                         &particlesPerThread, 1, 8)) {
        settings.particlesPerThread = static_cast<uint32_t>(particlesPerThread);
        changed = true;
    }

    changed |= ImGui::DragFloat(UiText("発生頻度###EmissionRate", "Emission rate###EmissionRate"),
                                &settings.emitRate, 0.5f, 0.1f, 120.0f);
    changed |= ImGui::DragFloat3(UiText("発生範囲###SpawnSize", "Spawn size###SpawnSize"),
                                 &settings.spawnOffsetScale.x, 0.01f, 0.0f, 2.0f);
    changed |= ImGui::ColorEdit4(UiText("色###EmitterColor", "Color###EmitterColor"),
                                 &settings.tintColor.x);
    changed |= ImGui::DragFloat(UiText("寿命###Lifetime", "Lifetime###Lifetime"),
                                &settings.baseLifeTime, 0.01f, 0.05f, 8.0f);
    changed |= ImGui::DragFloat(UiText("放射速度###RadialSpeed",
                                       "Radial speed###RadialSpeed"),
                                &settings.radialVelocity, 0.01f, 0.0f, 8.0f);
    changed |= ImGui::DragFloat(UiText("方向速度###DirectionalSpeed",
                                       "Directional speed###DirectionalSpeed"),
                                &settings.directionalVelocity,
                                0.01f, 0.0f, 8.0f);
    changed |= ImGui::DragFloat(UiText("乱流###Turbulence", "Turbulence###Turbulence"),
                                &settings.turbulence, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::DragFloat(UiText("伸び###Stretch", "Stretch###Stretch"),
                                &settings.stretch, 0.05f, 0.0f, 12.0f);
    changed |= ImGui::SliderFloat(UiText("光の影響###LightInfluence",
                                         "Light influence###LightInfluence"),
                                  &settings.lightInfluence, 0.0f, 1.0f);
    constexpr const char* kLightNamesEnglish[] = {"None", "Right hand", "Left hand"};
    constexpr const char* kLightNamesJapanese[] = {"なし", "右手", "左手"};
    const char* const* lightNames =
        japaneseUiEnabled_ ? kLightNamesJapanese : kLightNamesEnglish;
    int lightSelection = settings.assignedLight < 2u
                             ? static_cast<int>(settings.assignedLight) + 1
                             : 0;
    if (ImGui::Combo(UiText("割当ライト###AssignedLight",
                            "Assigned light###AssignedLight"),
                     &lightSelection, lightNames, IM_ARRAYSIZE(kLightNamesEnglish))) {
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
#ifdef ENABLE_IMGUI
    if (!ImGui::CollapsingHeader(UiText("フィールド###Fields", "Fields###Fields"),
                                 ImGuiTreeNodeFlags_DefaultOpen)) {
        return false;
    }

    constexpr const char* kFieldNamesEnglish[] = {"Directional", "Radial", "Vortex", "Drag"};
    constexpr const char* kFieldNamesJapanese[] = {"一定方向", "放射", "渦", "抵抗"};
    const char* const* fieldNames =
        japaneseUiEnabled_ ? kFieldNamesJapanese : kFieldNamesEnglish;
    bool changed = false;
    for (size_t i = 0; i < particleFields_.size(); ++i) {
        ParticleFieldSettings& field = particleFields_[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::SeparatorText(fieldNames[static_cast<size_t>(field.type)]);
        changed |= ImGui::Checkbox(UiText("有効###FieldEnabled", "Enabled###FieldEnabled"),
                                   &field.enabled);
        int type = static_cast<int>(field.type);
        if (ImGui::Combo(UiText("種類###FieldType", "Type###FieldType"), &type,
                         fieldNames, IM_ARRAYSIZE(kFieldNamesEnglish))) {
            field.type = static_cast<ParticleFieldType>(type);
            changed = true;
        }
        changed |= ImGui::DragFloat(UiText("範囲###FieldRadius", "Radius###FieldRadius"),
                                    &field.radius, 0.05f, 0.05f, 10.0f);
        changed |= ImGui::DragFloat(UiText("強さ###FieldStrength", "Strength###FieldStrength"),
                                    &field.strength, 0.05f, -10.0f, 10.0f);
        changed |= ImGui::DragFloat(UiText("減衰###FieldFalloff", "Falloff###FieldFalloff"),
                                    &field.falloff, 0.05f, 0.05f, 8.0f);
        changed |= ImGui::DragFloat3(UiText("方向###FieldDirection",
                                            "Direction###FieldDirection"),
                                     &field.direction.x, 0.02f, -1.0f, 1.0f);
        ImGui::PopID();
    }
    return changed;
#else
    return false;
#endif
}

void GameScene::DrawParticleEditor() {
#ifdef ENABLE_IMGUI
    if (!handParticlesReady_) {
        ImGui::TextUnformatted(UiText("GPUパーティクルを準備できませんでした。",
                                      "GPU Particle is not ready."));
        return;
    }

    ImGui::BeginChild("ParticleEditorScroll", ImVec2(0.0f, 0.0f), false);
    ImGui::Text(UiText("GPUスレッド: 256 | エミッター: %zu | フィールド: %zu",
                       "GPU threads: 256 | Emitters: %zu | Fields: %zu"),
                handParticles_.GetEmitterCount(), particleFields_.size());
    ImGui::TextUnformatted(UiText(
        "点 / 球 / 箱 / リング / 円盤 / 円弧 / 三角形 / メッシュ表面",
        "Point / Sphere / Box / Ring / Disk / Arc / Triangle / Mesh surface"));
    if (ImGui::Button(UiText("粒子を消去###ClearParticles",
                             "Clear particles###ClearParticles"))) {
        handParticles_.Clear();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox(UiText("軌跡###Trail", "Trail###Trail"),
                        &particleTrailEnabled_)) {
        particleMaterial_.params1.x = particleTrailEnabled_ ? 4.0f : 0.0f;
        handParticles_.SetMaterialSettings(particleMaterial_);
    }

    if (DrawParticleEmitterControls(UiText("右手エミッター###RightHandEmitter",
                                           "Right Hand Emitter###RightHandEmitter"),
                                    rightParticleEmitter_,
                                    rightParticleEmitterEnabled_)) {
        handParticles_.UpdateEmitter(rightParticleEmitterId_, rightParticleEmitter_);
        handParticles_.SetEmitterEnabled(rightParticleEmitterId_, rightParticleEmitterEnabled_);
    }
    if (DrawParticleEmitterControls(UiText("左手エミッター###LeftHandEmitter",
                                           "Left Hand Emitter###LeftHandEmitter"),
                                    leftParticleEmitter_,
                                    leftParticleEmitterEnabled_)) {
        handParticles_.UpdateEmitter(leftParticleEmitterId_, leftParticleEmitter_);
        handParticles_.SetEmitterEnabled(leftParticleEmitterId_, leftParticleEmitterEnabled_);
    }

    if (DrawParticleFieldControls()) {
        handParticles_.SetFields(particleFields_);
    }

    if (ImGui::CollapsingHeader(UiText("ライティング###Lighting",
                                       "Lighting###Lighting"),
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        bool lightingChanged = false;
        lightingChanged |= ImGui::DragFloat3(
            UiText("ライト方向###LightDirection", "Light direction###LightDirection"),
            &particleLighting_.direction.x, 0.02f, -1.0f, 1.0f);
        lightingChanged |=
            ImGui::DragFloat(UiText("ライト強度###LightIntensity",
                                    "Light intensity###LightIntensity"),
                             &particleLighting_.intensity, 0.05f, 0.0f, 8.0f);
        lightingChanged |= ImGui::ColorEdit3(
            UiText("ライト色###LightColor", "Light color###LightColor"),
            &particleLighting_.color.x);
        lightingChanged |= ImGui::ColorEdit3(
            UiText("環境光###Ambient", "Ambient###Ambient"),
            &particleLighting_.ambient.x);
        const char* assignedLightNames[] = {
            UiText("右手ライト", "Right hand light"),
            UiText("左手ライト", "Left hand light")};
        for (size_t i = 0; i < particleLighting_.pointLightCount; ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::SeparatorText(assignedLightNames[i]);
            lightingChanged |= ImGui::ColorEdit3(
                UiText("色###PointLightColor", "Color###PointLightColor"),
                &particleLighting_.pointLights[i].color.x);
            lightingChanged |= ImGui::DragFloat(
                UiText("強度###PointLightIntensity", "Intensity###PointLightIntensity"),
                                                &particleLighting_.pointLights[i].intensity,
                                                0.05f, 0.0f, 8.0f);
            lightingChanged |= ImGui::DragFloat(
                UiText("範囲###PointLightRange", "Range###PointLightRange"),
                &particleLighting_.pointLights[i].range, 0.05f, 0.05f, 8.0f);
            ImGui::PopID();
        }
        if (lightingChanged) {
            handParticles_.SetLightingSettings(particleLighting_);
        }
    }

    ImGui::EndChild();
#endif
}

void GameScene::DrawBoneLabels() {
    if (!debugLabelsEnabled_ || ctx_ == nullptr ||
        ctx_->rendering.model == nullptr || ctx_->rendering.text == nullptr ||
        !ctx_->rendering.text->IsReady()) {
        return;
    }

    const Model *model = ctx_->rendering.model->GetModel(humanModelId_);
    if (model == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < static_cast<uint32_t>(model->bones.size()); ++i) {
        const bool selected = static_cast<int>(i) == selectedBoneIndex_;
        if (!selected &&
            (debugMajorBonesOnly_ || model->bones.size() > 48u) &&
            !IsMajorDebugBone(model->bones[i].name)) {
            continue;
        }
        DrawBoneLabel(*model, i, selected);
    }
}

int GameScene::FindHoveredBone(const Model& model) const {
#ifdef ENABLE_IMGUI
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
    if (boneIndex >= model.bones.size() || ctx_ == nullptr ||
        ctx_->rendering.text == nullptr) {
        return;
    }

    XMFLOAT2 screen{};
    if (!ProjectWorldToScreen(BoneWorldPosition(boneIndex), screen)) {
        return;
    }

    const std::string label = DisplayBoneName(model.bones[boneIndex].name);
    const XMFLOAT2 position = selected
                                  ? XMFLOAT2{screen.x + 11.0f, screen.y - 25.0f}
                                  : XMFLOAT2{screen.x + 8.0f, screen.y + 5.0f};

    TextStyle shadow{};
    shadow.pixelSize = selected ? 17.0f : 13.0f;
    shadow.color = {0.0f, 0.0f, 0.0f, 0.95f};
    ctx_->rendering.text->DrawText(label, {position.x + 1.5f, position.y + 1.5f},
                                   shadow);

    TextStyle text = shadow;
    text.color = selected ? XMFLOAT4{1.0f, 0.90f, 0.22f, 1.0f}
                          : XMFLOAT4{0.62f, 0.90f, 1.0f, 1.0f};
    ctx_->rendering.text->DrawText(label, position, text);
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
#ifdef ENABLE_IMGUI
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

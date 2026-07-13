#pragma once

#include "Engine.h"

#ifdef DrawText
#undef DrawText
#endif

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class EvaluationScene final : public BaseScene {
  public:
    ~EvaluationScene() override;

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void SubmitLighting(LightingScene &lightingScene) override;
    void Draw() override;
    bool UsesForeground3DPass() const override { return false; }
    void DrawForeground3D() override;
    void DrawTransparent() override;
    void DrawPostProcessOverlay() override;

  private:
    void InitializeCamera();
    void InitializeModels();
    void InitializeParticles();
    void UpdateMovement(float deltaTime);
    void UpdateDebugControls();
    void UpdateModelAnimation(float deltaTime);
    void UpdateAttachmentPoints();
    void EmitHandParticles(float deltaTime);
    void DrawCharacter();
    void DrawWeapon();
    void DrawBoneRig();
    void DrawBindPoseRig();
    void DrawSceneProps();
    void DrawBoneDebugOverlay();
    void DrawDebugPanel();
    void DrawBoneLabels();

    uint32_t FindBoneIndex(const std::vector<std::string> &candidates) const;
    DirectX::XMFLOAT3 BoneWorldPosition(uint32_t boneIndex) const;
    DirectX::XMFLOAT3 BindBoneWorldPosition(uint32_t boneIndex) const;
    DirectX::XMMATRIX CharacterWorldMatrix() const;
    uint32_t BoneSegmentModelId(const std::string &boneName) const;
    bool IsMajorDebugBone(const std::string &boneName) const;
    std::string DisplayBoneName(const std::string &boneName) const;
    bool ProjectWorldToScreen(const DirectX::XMFLOAT3 &world,
                              DirectX::XMFLOAT2 &screen) const;
    Transform MakeTransformAt(const DirectX::XMFLOAT3 &position,
                              const DirectX::XMFLOAT3 &scale) const;
    Transform MakeBoneSegmentTransform(const DirectX::XMFLOAT3 &parent,
                                       const DirectX::XMFLOAT3 &child) const;
    void DrawScreenBoneLine(const DirectX::XMFLOAT3 &a,
                            const DirectX::XMFLOAT3 &b, uint32_t color,
                            float thickness);

    Camera camera_{};
    uint32_t humanModelId_ = kInvalidResourceId;
    uint32_t sneakModelId_ = kInvalidResourceId;
    uint32_t groundModelId_ = kInvalidResourceId;
    uint32_t weaponHandleModelId_ = kInvalidResourceId;
    uint32_t weaponGuardModelId_ = kInvalidResourceId;
    uint32_t weaponBladeModelId_ = kInvalidResourceId;
    uint32_t boneJointModelId_ = kInvalidResourceId;
    uint32_t boneCenterModelId_ = kInvalidResourceId;
    uint32_t boneLeftModelId_ = kInvalidResourceId;
    uint32_t boneRightModelId_ = kInvalidResourceId;
    uint32_t boneBindModelId_ = kInvalidResourceId;
    uint32_t boneSelectedModelId_ = kInvalidResourceId;
    uint32_t whiteTextureId_ = kInvalidResourceId;
    std::vector<DirectX::XMFLOAT4X4> bindPoseMatrices_;

    Transform characterTransform_{};
    float characterYaw_ = 0.0f;
    float walkTimeScale_ = 1.0f;
    float animationTime_ = 0.0f;
    float sneakBlend_ = 0.0f;
    float manualSneakBlend_ = -1.0f;

    uint32_t rightHandBone_ = kInvalidResourceId;
    uint32_t leftHandBone_ = kInvalidResourceId;
    uint32_t rightFootBone_ = kInvalidResourceId;
    uint32_t leftFootBone_ = kInvalidResourceId;

    DirectX::XMFLOAT3 rightHandWorld_{0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT3 leftHandWorld_{0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT3 rightFootWorld_{0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 leftFootWorld_{0.0f, 0.0f, 0.0f};

    bool debugRigEnabled_ = true;
    bool debugLabelsEnabled_ = true;
    bool debugBindPoseEnabled_ = false;
    bool debugMajorBonesOnly_ = false;
    int selectedBoneIndex_ = -1;

    GPUParticleSystem handParticles_{};
    bool handParticlesReady_ = false;
    float particleEmitAccumulator_ = 0.0f;
};

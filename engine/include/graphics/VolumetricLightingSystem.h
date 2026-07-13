#pragma once

#include "graphics/Lighting.h"

#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <memory>

class Camera;
class DirectXCommon;
class SrvManager;

struct VolumetricLightingSettings {
    bool enabled = false;
    DirectX::XMFLOAT3 sunDirection = {0.0f, 1.0f, 0.0f};
    float intensity = 0.0f;
    DirectX::XMFLOAT3 sunColor = {1.0f, 0.96f, 0.88f};
    float extinctionPerMeter = 0.00016f;
    float scatteringAlbedo = 0.92f;
    float anisotropy = 0.76f;
    float maxDistanceMeters = 180.0f;
    float densityScale = 1.0f;
    float heightFogBaseY = -1.0f;
    float heightFogFalloffMeters = 12.0f;
    float noiseStrength = 0.06f;
    float timeSeconds = 0.0f;
    uint32_t sampleCount = 12u;
    SceneShadowSettings shadow{};
};

class VolumetricLightingSystem {
public:
    VolumetricLightingSystem();
    ~VolumetricLightingSystem();

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, int width, int height);
    bool Finalize();
    bool Finalize(bool allowFrameAbort);
    bool Resize(int width, int height);

    void SetSettings(const VolumetricLightingSettings& settings);
    const VolumetricLightingSettings& GetSettings() const;
    bool IsReady() const;

    void Draw(D3D12_GPU_DESCRIPTOR_HANDLE depthHandle, D3D12_GPU_DESCRIPTOR_HANDLE shadowHandle,
              const Camera& camera, const DirectX::XMFLOAT4X4& lightViewProjection);

private:
    struct ConstantFrame;
    struct State;

    void CreateRootSignature();
    void CreateCompositeRootSignature();
    void CreatePipelineState();
    void CreateConstantBuffers();
    bool HasConstantBuffers() const;
    ConstantFrame* GetCurrentConstantFrame();
    const ConstantFrame* GetCurrentConstantFrame() const;
    bool EnsureRenderTextures();
    void DrawVolumeTexture(D3D12_GPU_DESCRIPTOR_HANDLE depthHandle,
                           D3D12_GPU_DESCRIPTOR_HANDLE shadowHandle,
                           D3D12_GPU_VIRTUAL_ADDRESS constantsAddress);
    void CompositeToScene();
    void CopyCurrentToHistory();

    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    std::unique_ptr<State> state_;
};

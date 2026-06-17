#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <memory>

class DirectXCommon;
class SrvManager;
struct Mesh;

class AdvancedGpuRenderer {
  public:
    struct MirrorDesc {
        DirectX::XMFLOAT3 center{0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 right{1.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 up{0.0f, 1.0f, 0.0f};
        DirectX::XMFLOAT3 normal{0.0f, 0.0f, -1.0f};
        float width = 1.0f;
        float height = 1.0f;
        float intensity = 1.0f;
    };

    AdvancedGpuRenderer();
    ~AdvancedGpuRenderer();

    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    uint32_t width, uint32_t height);
    bool Finalize();
    bool Finalize(bool allowFrameAbort);
    void Resize(uint32_t width, uint32_t height);

    bool IsRaytracingReady() const;

    void ResetRaytracingSceneInstances();
    void AddRaytracingMeshInstance(const Mesh &mesh,
                                   const DirectX::XMFLOAT4X4 &world,
                                   uint32_t instanceId);
    void DrawRaytracingScene(
        const DirectX::XMFLOAT4X4 &viewProjection,
        const DirectX::XMFLOAT3 &cameraPosition,
        const DirectX::XMFLOAT3 &sunDirection,
        const DirectX::XMFLOAT3 &anchorPosition, float timeSeconds);
    bool DrawRaytracedMirror(const MirrorDesc &mirror,
                             const DirectX::XMFLOAT3 &cameraPosition,
                             const DirectX::XMFLOAT3 &sunDirection,
                             const DirectX::XMFLOAT3 &anchorPosition,
                             float timeSeconds);
    D3D12_GPU_DESCRIPTOR_HANDLE GetRaytracingMirrorGpuHandle() const;

  private:
    struct RayConstantFrame;
    struct State;

    void CreateRaytracingResources();
    void CreateRaytracingOutput();
    void CreateRaytracingStateObject();
    void CreateRaytracingShaderTable();
    void CreateRaytracingAccelerationStructures();
    void CreateRaytracingCompositeResources();
    bool HasRayConstants() const;
    RayConstantFrame *GetCurrentRayConstantFrame();
    static void PopulateCommonRayConstants(
        RayConstantFrame &frame,
        const DirectX::XMFLOAT4X4 *viewProjection,
        const DirectX::XMFLOAT3 &cameraPosition,
        const DirectX::XMFLOAT3 &sunDirection,
        const DirectX::XMFLOAT3 &anchorPosition, float timeSeconds,
        uint32_t outputWidth, uint32_t outputHeight);
    void TransitionRayOutput(D3D12_RESOURCE_STATES nextState);
    void TransitionMirrorRayOutput(D3D12_RESOURCE_STATES nextState);
    bool BuildTopLevelAccelerationStructure();
    void DispatchRaytracingPass(ID3D12GraphicsCommandList4 *rayCmd,
                                RayConstantFrame &frame,
                                UINT descriptorOffset,
                                UINT rayGenerationRecordIndex,
                                uint32_t width, uint32_t height);

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    std::unique_ptr<State> state_;
};

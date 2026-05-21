#pragma once
#include "camera/Camera.h"
#include "graphics/Lighting.h"
#include "graphics/UploadRingBuffer.h"
#include "model/InstanceData.h"
#include "model/Material.h"
#include "model/MeshManager.h"
#include "model/ModelRenderer.h"
#include "model/Transform.h"
#include <DirectXMath.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl.h>

class DirectXCommon;
class SrvManager;
class TextureManager;

/// <summary>
/// モデルファイルに依存しない汎用メッシュ描画器
/// </summary>
class MeshRenderer {
  public:
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    TextureManager *textureManager);

    void BeginFrame();
    void PreDraw();
    void PostDraw();

    void DrawMesh(const Mesh &mesh, const Material &material,
                  const Transform &transform, const Camera &camera,
                  uint32_t textureId = 0,
                  uint32_t normalTextureId = UINT32_MAX);
    uint32_t CreatePipeline(const std::wstring &vertexShaderPath,
                            const std::wstring &pixelShaderPath);
    void DrawMeshWithPipeline(uint32_t pipelineId, const Mesh &mesh,
                              const Material &material,
                              const Transform &transform,
                              const Camera &camera, uint32_t textureId = 0,
                              uint32_t normalTextureId = UINT32_MAX);

    void DrawMeshInstanced(const Mesh &mesh, const Material &material,
                           const InstanceData *instances,
                           uint32_t instanceCount, const Camera &camera,
                           uint32_t textureId = 0,
                           uint32_t normalTextureId = UINT32_MAX);
    uint32_t CreateInstancedPipeline(
        const std::wstring &vertexShaderPath,
        const std::wstring &pixelShaderPath,
        const std::wstring &shadowVertexShaderPath,
        const std::wstring &shadowPixelShaderPath);
    void DrawMeshInstancedWithPipeline(
        uint32_t pipelineId, const Mesh &mesh, const Material &material,
        const InstanceData *instances, uint32_t instanceCount,
        const Camera &camera, uint32_t textureId = 0,
        uint32_t normalTextureId = UINT32_MAX);

    void PreDrawShadow();

    void DrawMeshShadow(const Mesh &mesh, const Transform &transform,
                        const DirectX::XMFLOAT4X4 &lightViewProjection);
    void DrawMeshShadow(const Mesh &mesh, const Material &material,
                        const Transform &transform,
                        const DirectX::XMFLOAT4X4 &lightViewProjection,
                        uint32_t textureId = 0);

    void DrawMeshInstancedShadow(
        const Mesh &mesh, const InstanceData *instances, uint32_t instanceCount,
        const DirectX::XMFLOAT4X4 &lightViewProjection);
    void DrawMeshInstancedShadow(
        const Mesh &mesh, const Material &material,
        const InstanceData *instances, uint32_t instanceCount,
        const DirectX::XMFLOAT4X4 &lightViewProjection, uint32_t textureId = 0);
    void DrawMeshInstancedShadowWithPipeline(
        uint32_t pipelineId, const Mesh &mesh, const Material &material,
        const InstanceData *instances, uint32_t instanceCount,
        const DirectX::XMFLOAT4X4 &lightViewProjection, uint32_t textureId = 0);

    void SetSceneLighting(const SceneLighting &lighting) {
        currentLighting_ = lighting;
    }
    void SetSceneFog(const SceneFog &fog) { currentFog_ = fog; }
    void SetCustomSceneParams(const DirectX::XMFLOAT4 &params0,
                              const DirectX::XMFLOAT4 &params1);
    void SetShadowMap(D3D12_GPU_DESCRIPTOR_HANDLE shadowMap,
                      const DirectX::XMFLOAT4X4 &lightViewProjection,
                      const SceneShadowSettings &settings);

  private:
    static constexpr uint32_t kMaxDraws = 4096;
    static constexpr size_t kUploadBytesPerFrame = 16 * 1024 * 1024;
    static constexpr size_t kPipelineVariantCount = 12;

    void CreateRootSignature();
    void CreatePipelineStates();
    void CreateShadowRootSignature();
    void CreateShadowPipelineStates();
    void CreateUploadBuffer();
    D3D12_GPU_VIRTUAL_ADDRESS WriteObjectConstants(
        const DirectX::XMMATRIX &wvp, const DirectX::XMMATRIX &world,
        const DirectX::XMMATRIX &worldInverseTranspose);
    D3D12_GPU_VIRTUAL_ADDRESS WriteSceneConstants(const Camera &camera);
    D3D12_GPU_VIRTUAL_ADDRESS WriteShadowSceneConstants(
        const DirectX::XMFLOAT4X4 &lightViewProjection);
    D3D12_GPU_VIRTUAL_ADDRESS WriteMaterialConstants(const Material &material);
    D3D12_VERTEX_BUFFER_VIEW WriteInstances(const InstanceData *instances,
                                            uint32_t instanceCount);
    void SetPipelineForMaterial(const Material &material);
    void SetPipelineForMaterial(
        const std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
                         kPipelineVariantCount> &pipelineStates,
        const Material &material);
    void SetInstancedPipelineForMaterial(const Material &material);
    void SetInstancedPipelineForMaterial(
        const std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
                         kPipelineVariantCount> &pipelineStates,
        const Material &material);

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    TextureManager *textureManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> shadowRootSignature_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
               kPipelineVariantCount>
        pipelineStates_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
               kPipelineVariantCount>
        instancedPipelineStates_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> instancedShadowPSO_;
    struct PipelineSet {
        std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
                   kPipelineVariantCount>
            pipelineStates;
    };
    struct InstancedPipelineSet {
        std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
                   kPipelineVariantCount>
            pipelineStates;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPipelineState;
    };
    std::vector<PipelineSet> customPipelines_;
    std::vector<InstancedPipelineSet> customInstancedPipelines_;

    UploadRingBuffer uploadBuffer_;
    uint32_t drawIndex_ = 0;

    SceneLighting currentLighting_{};
    SceneFog currentFog_{};
    D3D12_GPU_DESCRIPTOR_HANDLE shadowMapGpuHandle_{};
    DirectX::XMFLOAT4X4 shadowLightViewProjection_ = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4 shadowParams_{0.0f, 0.0015f, 0.45f, 0.0f};
    DirectX::XMFLOAT4 shadowFilterParams_{1.45f, 2600.0f, 0.045f, 0.0f};
    DirectX::XMFLOAT4 customSceneParams0_{1.0f, 0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 customSceneParams1_{0.0f, 1.0f, 0.24f, 0.0f};
};

#pragma once
#include "camera/Camera.h"
#include "graphics/Lighting.h"
#include "graphics/UploadRingBuffer.h"
#include "model/InstanceData.h"
#include "model/Material.h"
#include "model/MeshGpuCullBuffer.h"
#include "model/MeshInstanceBuffer.h"
#include "model/MeshManager.h"
#include "model/MeshPipelineFactory.h"
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
    ~MeshRenderer();

    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    TextureManager *textureManager);
    bool Finalize();
    bool Finalize(bool allowFrameAbort);

    /// <summary>
    /// Frameを開始する
    /// </summary>
    void BeginFrame();
    void PreDraw();
    void PostDraw();
    void InvalidateCommandState() noexcept;

    void DrawMesh(const Mesh &mesh, const Material &material,
                  const Transform &transform, const Camera &camera,
                  uint32_t textureId = 0,
                  uint32_t normalTextureId = UINT32_MAX);
    /// <summary>
    /// Pipelineを生成する
    /// </summary>
    uint32_t CreatePipeline(const MeshPipelineDesc &desc);
    uint32_t CreatePipeline(const std::wstring &vertexShaderPath,
                            const std::wstring &pixelShaderPath);
    uint32_t CreateAdditiveNoDepthPipeline(
        const std::wstring &vertexShaderPath,
        const std::wstring &pixelShaderPath);
    [[nodiscard]] bool ReleasePipeline(
        uint32_t pipelineId, bool allowFrameAbort = false) noexcept;
    size_t GetCustomPipelineCount() const noexcept {
        return customPipelines_.size();
    }
    void DrawMeshWithPipeline(uint32_t pipelineId, const Mesh &mesh,
                              const Material &material,
                              const Transform &transform,
                              const Camera &camera, uint32_t textureId = 0,
                              uint32_t normalTextureId = UINT32_MAX);
    void DrawMeshWithPipelineHandles(
        uint32_t pipelineId, const Mesh &mesh, const Material &material,
        const Transform &transform, const Camera &camera,
        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE normalTextureHandle);

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
    [[nodiscard]] bool ReleaseInstancedPipeline(
        uint32_t pipelineId, bool allowFrameAbort = false) noexcept;
    size_t GetCustomInstancedPipelineCount() const noexcept {
        return customInstancedPipelines_.size();
    }
    void DrawMeshInstancedWithPipeline(
        uint32_t pipelineId, const Mesh &mesh, const Material &material,
        const InstanceData *instances, uint32_t instanceCount,
        const Camera &camera, uint32_t textureId = 0,
        uint32_t normalTextureId = UINT32_MAX);
    void DrawMeshInstancedWithPipeline(
        uint32_t pipelineId, const Mesh &mesh, const Material &material,
        const MeshInstanceBuffer &instanceBuffer, const Camera &camera,
        uint32_t textureId = 0, uint32_t normalTextureId = UINT32_MAX);
    bool CreateStaticInstanceBuffer(const InstanceData *instances,
                                    uint32_t instanceCount,
                                    MeshInstanceBuffer &buffer);
    [[nodiscard]] bool
    ReleaseStaticInstanceBuffer(MeshInstanceBuffer &buffer,
                                bool allowFrameAbort = false) noexcept;
    bool DrawMeshInstancedGpuCulledWithPipeline(
        uint32_t pipelineId, const Mesh &mesh, const Material &material,
        const MeshInstanceBuffer &sourceInstances,
        MeshGpuCullBuffer &cullBuffer, const MeshGpuCullBounds &localBounds,
        const Camera &camera, float maxDistance, uint32_t textureId = 0,
        uint32_t normalTextureId = UINT32_MAX);
    bool DrawMeshInstancedGpuLodCulledWithPipeline(
        uint32_t pipelineId,
        const std::array<const Mesh *, kMeshGpuCullLodCount> &lodMeshes,
        const Material &material, const MeshInstanceBuffer &sourceInstances,
        MeshGpuLodCullBuffer &cullBuffer,
        const MeshGpuCullBounds &localBounds, const Camera &camera,
        const std::array<float, kMeshGpuCullLodCount - 1u> &distanceBreaks,
        uint32_t lodBias, float maxDistance, uint32_t textureId = 0,
        uint32_t normalTextureId = UINT32_MAX);
    bool DrawMeshInstancedGpuCulledShadowWithPipeline(
        uint32_t pipelineId, const Mesh &mesh, const Material &material,
        const MeshInstanceBuffer &sourceInstances,
        MeshGpuCullBuffer &cullBuffer, const MeshGpuCullBounds &localBounds,
        const DirectX::XMFLOAT4X4 &lightViewProjection,
        uint32_t textureId = 0);
    bool DrawMeshInstancedGpuLodCulledShadowWithPipeline(
        uint32_t pipelineId,
        const std::array<const Mesh *, kMeshGpuCullLodCount> &lodMeshes,
        const Material &material, const MeshInstanceBuffer &sourceInstances,
        MeshGpuLodCullBuffer &cullBuffer,
        const MeshGpuCullBounds &localBounds,
        const DirectX::XMFLOAT4X4 &lightViewProjection,
        const DirectX::XMFLOAT3 &lodOrigin,
        const std::array<float, kMeshGpuCullLodCount - 1u> &distanceBreaks,
        uint32_t lodBias, uint32_t textureId = 0);
    [[nodiscard]] bool
    ReleaseGpuCullBuffer(MeshGpuCullBuffer &buffer,
                         bool allowFrameAbort = false) noexcept;
    [[nodiscard]] bool
    ReleaseGpuLodCullBuffer(MeshGpuLodCullBuffer &buffer,
                            bool allowFrameAbort = false) noexcept;

    /// <summary>
    /// PreDrawShadowを実行する
    /// </summary>
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
    void DrawMeshInstancedShadowWithPipeline(
        uint32_t pipelineId, const Mesh &mesh, const Material &material,
        const MeshInstanceBuffer &instanceBuffer,
        const DirectX::XMFLOAT4X4 &lightViewProjection, uint32_t textureId = 0);

    void SetSceneLighting(const SceneLighting &lighting) {
        currentLighting_ = lighting;
        InvalidateConstantCaches();
    }
    void SetSceneFog(const SceneFog &fog) {
        currentFog_ = fog;
        InvalidateConstantCaches();
    }
    void SetCustomSceneParams(const DirectX::XMFLOAT4 &params0,
                              const DirectX::XMFLOAT4 &params1);
    void SetShadowMap(D3D12_GPU_DESCRIPTOR_HANDLE shadowMap,
                      const DirectX::XMFLOAT4X4 &lightViewProjection,
                      const SceneShadowSettings &settings);
    void SetOcclusionPyramid(D3D12_GPU_DESCRIPTOR_HANDLE depthPyramid,
                             const DirectX::XMMATRIX &viewProjection,
                             uint32_t width, uint32_t height,
                             uint32_t mipCount, float depthBias = 0.006f);
    void ClearOcclusionPyramid();
    bool IsReady() const;
    size_t GetUploadBytesPerFrame() const {
        return uploadBuffer_.GetBytesPerFrame();
    }
    size_t GetUploadTotalBytes() const { return uploadBuffer_.GetTotalBytes(); }
    size_t GetUploadFrameOffset() const {
        return uploadBuffer_.GetFrameOffset();
    }

  private:
    static constexpr uint32_t kMaxDraws = 4096;
    static constexpr size_t kUploadBytesPerFrame = 4 * 1024 * 1024;
    static constexpr size_t kPipelineVariantCount = kMeshPipelineVariantCount;

    struct ConstantCacheEntry {
        uint64_t hash = 0;
        D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
        bool valid = false;
    };
    enum class RootParameterKind : uint8_t {
        None,
        ConstantBuffer,
        DescriptorTable,
    };

    /// <summary>
    /// RootSignatureを生成する
    /// </summary>
    void CreateRootSignature();
    void CreatePipelineStates();
    void CreateShadowRootSignature();
    /// <summary>
    /// ShadowPipelineStatesを生成する
    /// </summary>
    void CreateShadowPipelineStates();
    void CreateGpuCullResources();
    bool CreateFallbackOcclusionTexture();
    void CreateUploadBuffer();
    void ResetResources();
    void InvalidateConstantCaches() noexcept;
    void SetGraphicsRootSignatureCached(ID3D12RootSignature *rootSignature);
    void SetPipelineStateCached(ID3D12PipelineState *pipelineState);
    void SetGraphicsRootConstantBufferViewCached(
        uint32_t rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address);
    void SetGraphicsRootDescriptorTableCached(
        uint32_t rootIndex, D3D12_GPU_DESCRIPTOR_HANDLE handle);
    void IASetVertexBuffersCached(uint32_t startSlot, uint32_t viewCount,
                                  const D3D12_VERTEX_BUFFER_VIEW *views);
    void IASetIndexBufferCached(const D3D12_INDEX_BUFFER_VIEW &view);
    void IASetPrimitiveTopologyCached(D3D12_PRIMITIVE_TOPOLOGY topology);
    D3D12_GPU_VIRTUAL_ADDRESS WriteObjectConstants(
        const DirectX::XMMATRIX &wvp, const DirectX::XMMATRIX &world,
        const DirectX::XMMATRIX &worldInverseTranspose);
    /// <summary>
    /// データを書き込む
    /// </summary>
    D3D12_GPU_VIRTUAL_ADDRESS WriteSceneConstants(const Camera &camera);
    D3D12_GPU_VIRTUAL_ADDRESS WriteShadowSceneConstants(
        const DirectX::XMFLOAT4X4 &lightViewProjection);
    /// <summary>
    /// データを書き込む
    /// </summary>
    D3D12_GPU_VIRTUAL_ADDRESS WriteMaterialConstants(const Material &material);
    D3D12_VERTEX_BUFFER_VIEW WriteInstances(const InstanceData *instances,
                                            uint32_t instanceCount);
    bool EnsureGpuCullBuffer(const MeshInstanceBuffer &sourceInstances,
                             MeshGpuCullBuffer &buffer);
    bool EnsureGpuLodCullBuffer(const MeshInstanceBuffer &sourceInstances,
                                MeshGpuLodCullBuffer &buffer);
    bool RegisterGpuCullStateRollback(MeshGpuCullBuffer &buffer);
    bool RegisterGpuLodCullStateRollback(MeshGpuLodCullBuffer &buffer);
    bool DrawInstancedWithPreparedBuffer(
        uint32_t pipelineId, const Mesh &mesh, const Material &drawMaterial,
        const D3D12_VERTEX_BUFFER_VIEW &instanceView, uint32_t instanceCount,
        const Camera &camera, uint32_t textureId, uint32_t normalTextureId);
    bool DrawInstancedShadowWithPreparedBuffer(
        uint32_t pipelineId, const Mesh &mesh, const Material &drawMaterial,
        const D3D12_VERTEX_BUFFER_VIEW &instanceView, uint32_t instanceCount,
        const DirectX::XMFLOAT4X4 &lightViewProjection, uint32_t textureId);
    /// <summary>
    /// PipelineForMaterialを設定する
    /// </summary>
    bool SetPipelineForMaterial(const Material &material);
    bool SetPipelineForMaterial(
        const std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
                         kPipelineVariantCount> &pipelineStates,
        const Material &material);
    /// <summary>
    /// InstancedPipelineForMaterialを設定する
    /// </summary>
    bool SetInstancedPipelineForMaterial(const Material &material);
    bool SetInstancedPipelineForMaterial(
        const std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
                         kPipelineVariantCount> &pipelineStates,
        const Material &material);
    D3D12_GPU_DESCRIPTOR_HANDLE GetCullOcclusionHandle() const;

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
    Microsoft::WRL::ComPtr<ID3D12RootSignature> gpuCullRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuCullPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuCullArgsPSO_;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> gpuCullCommandSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> gpuLodCullRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuLodCullPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuLodCullArgsPSO_;
    Microsoft::WRL::ComPtr<ID3D12Resource> fallbackOcclusionTexture_;
    uint32_t fallbackOcclusionSrvIndex_ = UINT32_MAX;
    D3D12_GPU_DESCRIPTOR_HANDLE fallbackOcclusionGpuHandle_{};
    struct InstancedPipelineSet {
        std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
                   kPipelineVariantCount>
            pipelineStates;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPipelineState;
    };
    std::vector<MeshPipelineSet> customPipelines_;
    std::vector<InstancedPipelineSet> customInstancedPipelines_;

    UploadRingBuffer uploadBuffer_;
    uint32_t drawIndex_ = 0;
    ID3D12RootSignature *cachedRootSignature_ = nullptr;
    ID3D12PipelineState *cachedPipelineState_ = nullptr;
    std::array<RootParameterKind, 6> cachedRootParameterKinds_{};
    std::array<uint64_t, 6> cachedRootParameterValues_{};
    std::array<D3D12_VERTEX_BUFFER_VIEW, 2> cachedVertexBufferViews_{};
    uint32_t cachedVertexBufferStartSlot_ = 0;
    uint32_t cachedVertexBufferViewCount_ = 0;
    bool cachedVertexBuffersValid_ = false;
    D3D12_INDEX_BUFFER_VIEW cachedIndexBufferView_{};
    bool cachedIndexBufferValid_ = false;
    D3D12_PRIMITIVE_TOPOLOGY cachedPrimitiveTopology_ =
        D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ConstantCacheEntry sceneConstantsCache_{};
    ConstantCacheEntry shadowSceneConstantsCache_{};
    ConstantCacheEntry materialConstantsCache_{};
    std::vector<InstanceData> instanceScratch_;

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
    DirectX::XMFLOAT4X4 occlusionViewProjection_ = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4 occlusionParams_{0.0f, 0.0f, 0.0f, 0.006f};
    D3D12_GPU_DESCRIPTOR_HANDLE occlusionPyramidGpuHandle_{};
    bool occlusionPyramidEnabled_ = false;
};

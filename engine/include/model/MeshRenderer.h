#pragma once
#include "core/ResourceHandle.h"
#include "camera/Camera.h"
#include "graphics/Lighting.h"
#include "model/InstanceData.h"
#include "model/Material.h"
#include "model/MeshGpuCullBuffer.h"
#include "model/MeshInstanceBuffer.h"
#include "model/MeshManager.h"
#include "model/MeshPipelineFactory.h"
#include "model/Transform.h"
#include <DirectXMath.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <string>
#include <vector>
#include <wrl.h>

class DirectXCommon;
struct MeshRendererCommandCache;
class SrvManager;
class TextureManager;

enum class MeshInstancedVertexLayout {
    Mesh,
    Tree,
};

enum class MeshVertexLayout {
    Mesh,
    Surface,
};

/// <summary>
/// モデルファイルに依存しない汎用メッシュ描画器
/// </summary>
class MeshRenderer {
  public:
    MeshRenderer();
    ~MeshRenderer();

    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    TextureManager *textureManager);
    bool Finalize();
    bool Finalize(bool allowFrameAbort);

    /// <summary>
    /// Frameを開始する
    /// </summary>
    void BeginFrame();
    void ReleaseCompletedFrameResources();
    void PreDraw();
    static void PostDraw();
    void InvalidateCommandState() noexcept;

    void DrawMesh(const Mesh &mesh, const Material &material,
                  const Transform &transform, const Camera &camera,
                  uint32_t textureId = 0,
                  uint32_t normalTextureId = kInvalidResourceId);
    /// <summary>
    /// Pipelineを生成する
    /// </summary>
    uint32_t CreatePipeline(const MeshPipelineDesc &desc);
    uint32_t CreatePipeline(const MeshPipelineDesc &desc,
                            MeshVertexLayout vertexLayout);
    uint32_t CreatePipeline(const MeshPipelineDesc &desc,
                            D3D12_INPUT_LAYOUT_DESC inputLayout);
    uint32_t CreatePipeline(const std::wstring &vertexShaderPath,
                            const std::wstring &pixelShaderPath);
    uint32_t CreatePipeline(const std::wstring &vertexShaderPath,
                            const std::wstring &pixelShaderPath,
                            MeshVertexLayout vertexLayout);
    uint32_t CreateAdditiveNoDepthPipeline(
        const std::wstring &vertexShaderPath,
        const std::wstring &pixelShaderPath);
    [[nodiscard]] bool ReleasePipeline(
        uint32_t pipelineId, bool allowFrameAbort = false) noexcept;
    size_t GetCustomPipelineCount() const noexcept;
    void DrawMeshWithPipeline(uint32_t pipelineId, const Mesh &mesh,
                              const Material &material,
                              const Transform &transform,
                              const Camera &camera, uint32_t textureId = 0,
                              uint32_t normalTextureId = kInvalidResourceId);
    void DrawMeshWithPipelineHandles(
        uint32_t pipelineId, const Mesh &mesh, const Material &material,
        const Transform &transform, const Camera &camera,
        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE normalTextureHandle);

    void DrawMeshInstanced(const Mesh &mesh, const Material &material,
                           const InstanceData *instances,
                           uint32_t instanceCount, const Camera &camera,
                           uint32_t textureId = 0,
                           uint32_t normalTextureId = kInvalidResourceId);
    uint32_t CreateInstancedPipeline(
        const std::wstring &vertexShaderPath,
        const std::wstring &pixelShaderPath,
        const std::wstring &shadowVertexShaderPath,
        const std::wstring &shadowPixelShaderPath);
    uint32_t CreateInstancedPipeline(
        const std::wstring &vertexShaderPath,
        const std::wstring &pixelShaderPath,
        const std::wstring &shadowVertexShaderPath,
        const std::wstring &shadowPixelShaderPath,
        MeshInstancedVertexLayout vertexLayout);
    uint32_t CreateInstancedPipeline(
        const std::wstring &vertexShaderPath,
        const std::wstring &pixelShaderPath,
        const std::wstring &shadowVertexShaderPath,
        const std::wstring &shadowPixelShaderPath,
        D3D12_INPUT_LAYOUT_DESC inputLayout);
    [[nodiscard]] bool ReleaseInstancedPipeline(
        uint32_t pipelineId, bool allowFrameAbort = false) noexcept;
    size_t GetCustomInstancedPipelineCount() const noexcept;
    void DrawMeshInstancedWithPipeline(
        uint32_t pipelineId, const Mesh &mesh, const Material &material,
        const InstanceData *instances, uint32_t instanceCount,
        const Camera &camera, uint32_t textureId = 0,
        uint32_t normalTextureId = kInvalidResourceId);
    void DrawMeshInstancedWithPipeline(
        uint32_t pipelineId, const Mesh &mesh, const Material &material,
        const MeshInstanceBuffer &instanceBuffer, const Camera &camera,
        uint32_t textureId = 0, uint32_t normalTextureId = kInvalidResourceId);
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
        uint32_t normalTextureId = kInvalidResourceId,
        float minDistance = 0.0f);
    bool DrawMeshInstancedGpuLodCulledWithPipeline(
        uint32_t pipelineId,
        const std::array<const Mesh *, kMeshGpuCullLodCount> &lodMeshes,
        const Material &material, const MeshInstanceBuffer &sourceInstances,
        MeshGpuLodCullBuffer &cullBuffer,
        const MeshGpuCullBounds &localBounds, const Camera &camera,
        const std::array<float, kMeshGpuCullLodCount - 1u> &distanceBreaks,
        uint32_t lodBias, float maxDistance, uint32_t textureId = 0,
        uint32_t normalTextureId = kInvalidResourceId);
    bool DrawMeshInstancedGpuCulledShadowWithPipeline(
        uint32_t pipelineId, const Mesh &mesh, const Material &material,
        const MeshInstanceBuffer &sourceInstances,
        MeshGpuCullBuffer &cullBuffer, const MeshGpuCullBounds &localBounds,
        const DirectX::XMFLOAT4X4 &lightViewProjection,
        const DirectX::XMFLOAT3 &cullOrigin, float maxDistance,
        uint32_t textureId = 0, bool opaqueShadow = false,
        float minDistance = 0.0f);
    bool DrawMeshInstancedGpuLodCulledShadowWithPipeline(
        uint32_t pipelineId,
        const std::array<const Mesh *, kMeshGpuCullLodCount> &lodMeshes,
        const Material &material, const MeshInstanceBuffer &sourceInstances,
        MeshGpuLodCullBuffer &cullBuffer,
        const MeshGpuCullBounds &localBounds,
        const DirectX::XMFLOAT4X4 &lightViewProjection,
        const DirectX::XMFLOAT3 &lodOrigin,
        const std::array<float, kMeshGpuCullLodCount - 1u> &distanceBreaks,
        uint32_t lodBias, float maxDistance, uint32_t textureId = 0,
        bool opaqueShadow = false);
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
        const DirectX::XMFLOAT4X4 &lightViewProjection, uint32_t textureId = 0,
        bool opaqueShadow = false);
    void DrawMeshInstancedShadowWithPipeline(
        uint32_t pipelineId, const Mesh &mesh, const Material &material,
        const MeshInstanceBuffer &instanceBuffer,
        const DirectX::XMFLOAT4X4 &lightViewProjection, uint32_t textureId = 0,
        bool opaqueShadow = false);

    void SetSceneLighting(const SceneLighting &lighting);
    void SetSceneFog(const SceneFog &fog);
    void SetEnvironmentTexture(uint32_t textureId);
    void SetMaterialReflectionsEnabled(bool enabled);
    void SetCustomSceneParams(const DirectX::XMFLOAT4 &params0,
                              const DirectX::XMFLOAT4 &params1);
    void SetShadowMap(D3D12_GPU_DESCRIPTOR_HANDLE shadowMap,
                      const DirectX::XMFLOAT4X4 &lightViewProjection,
                      const SceneShadowSettings &settings);
    void SetSpotLightShadowMap(D3D12_GPU_DESCRIPTOR_HANDLE shadowMap,
                               const DirectX::XMFLOAT4X4 &lightViewProjection,
                               const SceneShadowSettings &settings);
    void SetOcclusionPyramid(D3D12_GPU_DESCRIPTOR_HANDLE depthPyramid,
                             const DirectX::XMMATRIX &viewProjection,
                             uint32_t width, uint32_t height,
                             uint32_t mipCount, float depthBias = 0.006f);
    void ClearOcclusionPyramid();
    bool IsReady() const;
    size_t GetUploadBytesPerFrame() const;
    size_t GetUploadTotalBytes() const;
    size_t GetUploadFrameOffset() const;

  private:
    static constexpr uint32_t kMaxDraws = 4096;
    static constexpr size_t kUploadBytesPerFrame = 4 * 1024 * 1024;
    static constexpr size_t kPipelineVariantCount = kMeshPipelineVariantCount;
    using PipelineStateArray = MeshPipelineStateArray;

    struct ConstantCacheEntry {
        uint64_t hash = 0;
        D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
        bool valid = false;
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
    void PreDrawWithRootSignature(ID3D12RootSignature *rootSignature);
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
    void BindForwardMaterialDescriptors(const Material &drawMaterial,
                                        uint32_t textureId,
                                        uint32_t normalTextureId);
    void BindForwardMaterialDescriptorHandles(
        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE normalTextureHandle);
    void BindShadowMaterialDescriptor(const Material &drawMaterial,
                                      uint32_t textureId);
    void SubmitForwardMeshDraw(
        const Mesh &mesh, const Material &drawMaterial,
        D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr,
        const D3D12_VERTEX_BUFFER_VIEW *vertexViews, uint32_t vertexViewCount,
        uint32_t instanceCount, uint32_t textureId, uint32_t normalTextureId);
    void SubmitForwardMeshDrawWithHandles(
        const Mesh &mesh, D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr,
        const D3D12_VERTEX_BUFFER_VIEW *vertexViews, uint32_t vertexViewCount,
        uint32_t instanceCount, D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE normalTextureHandle);
    void SubmitShadowMeshDraw(
        const Mesh &mesh, const Material &drawMaterial,
        D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr,
        const D3D12_VERTEX_BUFFER_VIEW *vertexViews, uint32_t vertexViewCount,
        uint32_t instanceCount, uint32_t textureId);
    D3D12_GPU_VIRTUAL_ADDRESS WriteObjectConstants(
        const DirectX::XMMATRIX &wvp, const DirectX::XMMATRIX &world,
        const DirectX::XMMATRIX &worldInverseTranspose);
    bool WriteForwardTransformedDrawConstants(
        const Material &drawMaterial, const Transform &transform,
        const Camera &camera, D3D12_GPU_VIRTUAL_ADDRESS &objectCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS &sceneCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS &materialCbAddr);
    bool WriteForwardIdentityDrawConstants(
        const Material &drawMaterial, const Camera &camera,
        D3D12_GPU_VIRTUAL_ADDRESS &objectCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS &sceneCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS &materialCbAddr);
    bool WriteShadowTransformedDrawConstants(
        const Material &drawMaterial, const Transform &transform,
        const DirectX::XMFLOAT4X4 &lightViewProjection,
        D3D12_GPU_VIRTUAL_ADDRESS &objectCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS &sceneCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS &materialCbAddr);
    bool WriteShadowIdentityDrawConstants(
        const Material &drawMaterial,
        const DirectX::XMFLOAT4X4 &lightViewProjection,
        D3D12_GPU_VIRTUAL_ADDRESS &objectCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS &sceneCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS &materialCbAddr);
    bool PrepareForwardMeshDrawConstants(
        const Mesh &mesh, const Material &material,
        const Transform &transform, const Camera &camera,
        Material &drawMaterial, D3D12_GPU_VIRTUAL_ADDRESS &objectCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS &sceneCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS &materialCbAddr);
    bool DrawForwardMeshWithPipelineStates(
        const Mesh &mesh, const Material &material,
        const Transform &transform, const Camera &camera,
        const PipelineStateArray *pipelineStates, uint32_t textureId,
        uint32_t normalTextureId);
    bool DrawForwardMeshWithPipelineHandles(
        const Mesh &mesh, const Material &material,
        const Transform &transform, const Camera &camera,
        const PipelineStateArray &pipelineStates,
        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE normalTextureHandle);
    bool DrawForwardInstancedWithPreparedBuffer(
        const Mesh &mesh, const Material &drawMaterial,
        const D3D12_VERTEX_BUFFER_VIEW &instanceView,
        uint32_t instanceCount, const Camera &camera,
        const PipelineStateArray *pipelineStates, uint32_t textureId,
        uint32_t normalTextureId);
    bool DrawShadowInstancedWithPreparedBuffer(
        const Mesh &mesh, const Material &drawMaterial,
        const D3D12_VERTEX_BUFFER_VIEW &instanceView,
        uint32_t instanceCount,
        const DirectX::XMFLOAT4X4 &lightViewProjection,
        const PipelineStateArray *pipelineStates, uint32_t textureId);
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
    bool PrepareGpuCullDispatch(
        const MeshInstanceBuffer &sourceInstances, MeshGpuCullBuffer &buffer,
        ID3D12GraphicsCommandList *&commandList,
        ID3D12DescriptorHeap *&descriptorHeap,
        D3D12_GPU_DESCRIPTOR_HANDLE &occlusionHandle);
    bool PrepareGpuLodCullDispatch(
        const MeshInstanceBuffer &sourceInstances,
        MeshGpuLodCullBuffer &buffer, ID3D12GraphicsCommandList *&commandList,
        ID3D12DescriptorHeap *&descriptorHeap,
        D3D12_GPU_DESCRIPTOR_HANDLE &occlusionHandle);
    bool DispatchSingleGpuCull(
        const Mesh &mesh, const MeshInstanceBuffer &sourceInstances,
        MeshGpuCullBuffer &cullBuffer,
        const MeshGpuCullBounds &localBounds,
        const DirectX::XMMATRIX &cullViewProjection,
        const DirectX::XMFLOAT3 &cullOrigin,
        const DirectX::XMFLOAT4 &occlusionParams, float maxDistance,
        float minDistance, ID3D12GraphicsCommandList *&commandList);
    bool DispatchLodGpuCull(
        const std::array<const Mesh *, kMeshGpuCullLodCount> &lodMeshes,
        const MeshInstanceBuffer &sourceInstances,
        MeshGpuLodCullBuffer &cullBuffer,
        const MeshGpuCullBounds &localBounds,
        const DirectX::XMMATRIX &cullViewProjection,
        const DirectX::XMFLOAT3 &cullOrigin,
        const DirectX::XMFLOAT3 &lodOrigin,
        const std::array<float, kMeshGpuCullLodCount - 1u> &distanceBreaks,
        const DirectX::XMFLOAT4 &occlusionParams, uint32_t lodBias,
        float maxDistance, ID3D12GraphicsCommandList *&commandList);
    bool BindGpuCulledForwardDrawState(uint32_t pipelineId,
                                       const Material &material,
                                       const Camera &camera,
                                       uint32_t textureId,
                                       uint32_t normalTextureId);
    bool BindGpuCulledShadowDrawState(
        uint32_t pipelineId, const Material &material,
        const DirectX::XMFLOAT4X4 &lightViewProjection, uint32_t textureId,
        bool opaqueShadow);
    void ExecuteGpuCulledMeshDraw(ID3D12GraphicsCommandList *commandList,
                                  const Mesh &mesh,
                                  const MeshGpuCullBuffer &cullBuffer);
    bool ExecuteGpuLodCulledMeshDraws(
        ID3D12GraphicsCommandList *commandList,
        const std::array<const Mesh *, kMeshGpuCullLodCount> &lodMeshes,
        const MeshGpuLodCullBuffer &cullBuffer);
    bool DrawInstancedWithPreparedBuffer(
        uint32_t pipelineId, const Mesh &mesh, const Material &drawMaterial,
        const D3D12_VERTEX_BUFFER_VIEW &instanceView, uint32_t instanceCount,
        const Camera &camera, uint32_t textureId, uint32_t normalTextureId);
    bool DrawInstancedShadowWithPreparedBuffer(
        uint32_t pipelineId, const Mesh &mesh, const Material &drawMaterial,
        const D3D12_VERTEX_BUFFER_VIEW &instanceView, uint32_t instanceCount,
        const DirectX::XMFLOAT4X4 &lightViewProjection, uint32_t textureId,
        bool opaqueShadow);
    void MarkStaticInstanceBufferUsed(const MeshInstanceBuffer &buffer) const;
    bool RetireStaticInstanceBuffer(MeshInstanceBuffer &buffer) noexcept;
    /// <summary>
    /// PipelineForMaterialを設定する
    /// </summary>
    bool SetPipelineForMaterial(const Material &material);
    bool SetPipelineForMaterial(const PipelineStateArray &pipelineStates,
                                const Material &material);
    /// <summary>
    /// InstancedPipelineForMaterialを設定する
    /// </summary>
    bool SetInstancedPipelineForMaterial(const Material &material);
    bool SetInstancedPipelineForMaterial(
        const PipelineStateArray &pipelineStates, const Material &material);
    bool SetInstancedShadowPipelineForMaterial(
        const PipelineStateArray &pipelineStates, const Material &material);
    bool SetPipelineStateForMaterial(const PipelineStateArray &pipelineStates,
                                     const Material &material);
    D3D12_GPU_DESCRIPTOR_HANDLE GetCullOcclusionHandle() const;

    struct InstancedPipelineSet;
    struct State;
    std::unique_ptr<State> state_;
};

#pragma once
#include "core/ResourceHandle.h"
#include "camera/Camera.h"
#include "graphics/Lighting.h"
#include "model/InstanceData.h"
#include "model/MaterialManager.h"
#include "model/ModelDrawEffect.h"
#include "model/Model.h"
#include "model/Transform.h"
#include <DirectXMath.h>
#include <cstddef>
#include <d3d12.h>
#include <memory>
#include <vector>

class DirectXCommon;
class SrvManager;
class MeshManager;
class TextureManager;

/// <summary>
/// モデル描画パイプラインと定数バッファ更新を担当する
/// </summary>
class ModelRenderer {
  public:
    ModelRenderer();
    ~ModelRenderer();

    /// <summary>
    /// モデル描画に必要なパイプラインと各マネージャ参照を初期化する
    /// </summary>
    /// <param name="dxCommon">DirectX共通管理</param>
    /// <param name="srvManager">SRV管理</param>
    /// <param name="meshManager">メッシュ管理</param>
    /// <param name="textureManager">テクスチャ管理</param>
    /// <param name="materialManager">マテリアル管理</param>
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    MeshManager *meshManager, TextureManager *textureManager,
                    MaterialManager *materialManager);
    bool Finalize();
    bool Finalize(bool allowFrameAbort);

    /// <summary>
    /// Frameを開始する
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// 指定モデルをTransformとカメラに基づいて描画する
    /// </summary>
    /// <param name="model">描画するモデル</param>
    /// <param name="transform">描画するモデルのTransform</param>
    /// <param name="camera">描画に使用するカメラ</param>
    /// <param
    /// name="environmentTextureId">この描画で使用する環境マップテクスチャID。invalidの場合はSetEnvironmentTextureの設定を使用</param>
    void Draw(const Model &model, const Transform &transform,
              const Camera &camera,
              uint32_t environmentTextureId = kInvalidResourceId);

    /// <summary>
    /// 同一モデルを複数Transformでまとめて描画する
    /// </summary>
    void DrawInstanced(const Model &model, const Transform *transforms,
                       uint32_t instanceCount, const Camera &camera,
                       uint32_t environmentTextureId = kInvalidResourceId);

    /// <summary>
    /// 同一モデルを複数InstanceDataでまとめて描画する
    /// </summary>
    void DrawInstanced(const Model &model, const InstanceData *instances,
                       uint32_t instanceCount, const Camera &camera,
                       uint32_t environmentTextureId = kInvalidResourceId);

    /// <summary>
    /// ShadowPass用の描画状態を設定する
    /// </summary>
    void PreDrawShadow();

    /// <summary>
    /// 指定モデルをShadowMapへ深度描画する
    /// </summary>
    void DrawShadow(const Model &model, const Transform &transform,
                    const DirectX::XMFLOAT4X4 &lightViewProjection);

    /// <summary>
    /// 同一モデルを複数TransformでShadowMapへまとめて深度描画する
    /// </summary>
    void DrawInstancedShadow(const Model &model, const Transform *transforms,
                             uint32_t instanceCount,
                             const DirectX::XMFLOAT4X4 &lightViewProjection);

    /// <summary>
    /// 同一モデルを複数InstanceDataでShadowMapへまとめて深度描画する
    /// </summary>
    void DrawInstancedShadow(const Model &model, const InstanceData *instances,
                             uint32_t instanceCount,
                             const DirectX::XMFLOAT4X4 &lightViewProjection);

    /// <summary>
    /// 描画前に必要なGPUスキニングを実行する
    /// </summary>
    void PrepareSkinning(const Model &model);
    void PrepareSkinning(const std::vector<const Model *> &models);

    /// <summary>
    /// シーンライティングを設定する
    /// </summary>
    /// <param name="lighting">適用するライティング定数</param>
    void SetSceneLighting(const SceneLighting &lighting);

    /// <summary>
    /// 現在フレームの描画エフェクトを設定する
    /// </summary>
    void SetDrawEffect(const ModelDrawEffect &effect);

    /// <summary>
    /// 描画エフェクト設定を初期状態へ戻す
    /// </summary>
    void ClearDrawEffect();

    /// <summary>
    /// シーンフォグを設定する
    /// </summary>
    /// <param name="fog">適用するフォグ定数</param>
    void SetSceneFog(const SceneFog &fog);

    /// <summary>
    /// 環境マップに使うキューブマップテクスチャを設定する
    /// </summary>
    /// <param name="textureId">キューブマップのテクスチャID</param>
    void SetEnvironmentTexture(uint32_t textureId);

    /// <summary>
    /// 環境マップを無効化する
    /// </summary>
    void ClearEnvironmentTexture();

    /// <summary>
    /// 標準シェーダーが参照するShadowMapを設定する
    /// </summary>
    void SetShadowMap(D3D12_GPU_DESCRIPTOR_HANDLE shadowMap,
                      const DirectX::XMFLOAT4X4 &lightViewProjection,
                      const SceneShadowSettings &settings);
    void SetSpotLightShadowMap(D3D12_GPU_DESCRIPTOR_HANDLE shadowMap,
                               const DirectX::XMFLOAT4X4 &lightViewProjection,
                               const SceneShadowSettings &settings);

    /// <summary>
    /// モデル用スキンクラスターGPUリソースを生成する
    /// </summary>
    /// <param name="model">対象モデル</param>
    bool CreateSkinClusters(Model &model);

    /// <summary>
    /// スキンクラスターのパレット内容を更新する
    /// </summary>
    /// <param name="model">対象モデル</param>
    void UpdateSkinClusters(Model &model);

    /// <summary>
    /// モデル描画用パイプラインを描画前に設定する
    /// </summary>
    void PreDraw();

    /// <summary>
    /// モデル描画後の状態を整理する
    /// </summary>
    static void PostDraw();
    bool IsReady() const;
    size_t GetUploadBytesPerFrame() const;
    size_t GetUploadTotalBytes() const;
    size_t GetUploadFrameOffset() const;

  private:
    /// <summary>
    /// ルートシグネチャを生成する
    /// </summary>
    void CreateRootSignature();

    /// <summary>
    /// ComputeShader用ルートシグネチャを生成する
    /// </summary>
    void CreateSkinningRootSignature();

    /// <summary>
    /// パイプラインステートを生成する
    /// </summary>
    void CreatePipelineState();

    /// <summary>
    /// ShadowPass用のルートシグネチャを生成する
    /// </summary>
    void CreateShadowRootSignature();

    /// <summary>
    /// ShadowPass用のパイプラインステートを生成する
    /// </summary>
    void CreateShadowPipelineState();

    /// <summary>
    /// ComputeShader用パイプラインステートを生成する
    /// </summary>
    void CreateSkinningPipelineState();

    void CreateUploadBuffer();
    bool CreateIdentityPalette();
    void ResetIdentityPalette() noexcept;
    bool HasIdentityPaletteResources() const noexcept;
    D3D12_GPU_VIRTUAL_ADDRESS GetIdentityPaletteAddress() const;
    void ResetResources();
    D3D12_GPU_VIRTUAL_ADDRESS WriteObjectConstants(
        const DirectX::XMMATRIX &wvp, const DirectX::XMMATRIX &world,
        const DirectX::XMMATRIX &worldInverseTranspose);
    /// <summary>
    /// データを書き込む
    /// </summary>
    D3D12_GPU_VIRTUAL_ADDRESS WriteSceneConstants(const Camera &camera);
    D3D12_GPU_VIRTUAL_ADDRESS WriteDrawEffectConstants();
    D3D12_VERTEX_BUFFER_VIEW WriteInstances(const Model &model,
                                            const Transform *transforms,
                                            uint32_t instanceCount);
    D3D12_VERTEX_BUFFER_VIEW WriteInstances(const Model &model,
                                            const InstanceData *instances,
                                            uint32_t instanceCount);
    void DrawInstancedWithPreparedBuffer(
        const Model &model, const D3D12_VERTEX_BUFFER_VIEW &instanceView,
        uint32_t instanceCount, const Camera &camera,
        uint32_t environmentTextureId);
    bool SubmitForwardSubMeshDraw(
        const ModelSubMesh &subMesh, D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr,
        D3D12_GPU_VIRTUAL_ADDRESS effectCbAddr, uint32_t environmentTextureId,
        D3D12_GPU_VIRTUAL_ADDRESS identityPaletteAddress,
        const D3D12_VERTEX_BUFFER_VIEW *instanceView, uint32_t instanceCount,
        bool instanced);
    bool SubmitShadowSubMeshDraw(
        const ModelSubMesh &subMesh, D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr,
        ID3D12PipelineState *pipelineState,
        const D3D12_VERTEX_BUFFER_VIEW *instanceView, uint32_t instanceCount);
    /// <summary>
    /// PipelineForMaterialを設定する
    /// </summary>
    bool SetPipelineForMaterial(const Material &material);
    bool SetInstancedPipelineForMaterial(const Material &material);

    /// <summary>
    /// ComputeShaderで必要なスキニング済み頂点をまとめて書き込む
    /// </summary>
    void DispatchSkinningBatch(const Model &model);
    void DispatchSkinningBatch(const std::vector<const Model *> &models);
    void DispatchSkinningJobs(const std::vector<const ModelSubMesh *> &jobs);
    /// <summary>
    /// DispatchSkinningを実行する
    /// </summary>
    void DispatchSkinning(const ModelSubMesh &subMesh);
    bool NeedsSkinningDispatch(const ModelSubMesh &subMesh) const;

  private:
    static constexpr uint32_t kMaxDraws = 4096;
    static constexpr size_t kUploadBytesPerFrame = 4 * 1024 * 1024;
    static constexpr size_t kPipelineVariantCount = 18;

    struct State;
    std::unique_ptr<State> state_;
};

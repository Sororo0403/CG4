#pragma once
#include "camera/Camera.h"
#include "graphics/Lighting.h"
#include "graphics/UploadRingBuffer.h"
#include "model/InstanceData.h"
#include "model/MaterialManager.h"
#include "model/Model.h"
#include "model/Transform.h"
#include <DirectXMath.h>
#include <cstddef>
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;
class SrvManager;
class MeshManager;
class TextureManager;

/// <summary>
/// モデル描画パイプラインと定数バッファ更新を担当する
/// </summary>
class ModelRenderer {
  public:
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

    void BeginFrame();

    /// <summary>
    /// 指定モデルをTransformとカメラに基づいて描画する
    /// </summary>
    /// <param name="model">描画するモデル</param>
    /// <param name="transform">描画するモデルのTransform</param>
    /// <param name="camera">描画に使用するカメラ</param>
    /// <param
    /// name="environmentTextureId">この描画で使用する環境マップテクスチャID。UINT32_MAXの場合はSetEnvironmentTextureの設定を使用</param>
    void Draw(const Model &model, const Transform &transform,
              const Camera &camera, uint32_t environmentTextureId = UINT32_MAX);

    /// <summary>
    /// 同一モデルを複数Transformでまとめて描画する
    /// </summary>
    void DrawInstanced(const Model &model, const Transform *transforms,
                       uint32_t instanceCount, const Camera &camera,
                       uint32_t environmentTextureId = UINT32_MAX);

    /// <summary>
    /// 同一モデルを複数InstanceDataでまとめて描画する
    /// </summary>
    void DrawInstanced(const Model &model, const InstanceData *instances,
                       uint32_t instanceCount, const Camera &camera,
                       uint32_t environmentTextureId = UINT32_MAX);

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
    /// シーンライティングを設定する
    /// </summary>
    /// <param name="lighting">適用するライティング定数</param>
    void SetSceneLighting(const SceneLighting &lighting) {
        currentLighting_ = lighting;
    }

    /// <summary>
    /// シーンフォグを設定する
    /// </summary>
    /// <param name="fog">適用するフォグ定数</param>
    void SetSceneFog(const SceneFog &fog) { currentFog_ = fog; }

    /// <summary>
    /// 環境マップに使うキューブマップテクスチャを設定する
    /// </summary>
    /// <param name="textureId">キューブマップのテクスチャID</param>
    void SetEnvironmentTexture(uint32_t textureId) {
        environmentTextureId_ = textureId;
        hasEnvironmentTexture_ = true;
    }

    /// <summary>
    /// 環境マップを無効化する
    /// </summary>
    void ClearEnvironmentTexture() { hasEnvironmentTexture_ = false; }

    /// <summary>
    /// 標準シェーダーが参照するShadowMapを設定する
    /// </summary>
    void SetShadowMap(D3D12_GPU_DESCRIPTOR_HANDLE shadowMap,
                      const DirectX::XMFLOAT4X4 &lightViewProjection,
                      const SceneShadowSettings &settings);

    /// <summary>
    /// モデル用スキンクラスターGPUリソースを生成する
    /// </summary>
    /// <param name="model">対象モデル</param>
    void CreateSkinClusters(Model &model);

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
    void PostDraw();

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
    D3D12_GPU_VIRTUAL_ADDRESS WriteObjectConstants(
        const DirectX::XMMATRIX &wvp, const DirectX::XMMATRIX &world,
        const DirectX::XMMATRIX &worldInverseTranspose);
    D3D12_GPU_VIRTUAL_ADDRESS WriteSceneConstants(const Camera &camera);
    D3D12_VERTEX_BUFFER_VIEW WriteInstances(const Model &model,
                                            const Transform *transforms,
                                            uint32_t instanceCount);
    D3D12_VERTEX_BUFFER_VIEW WriteInstances(const Model &model,
                                            const InstanceData *instances,
                                            uint32_t instanceCount);
    void SetPipelineForMaterial(const Material &material);
    void SetInstancedPipelineForMaterial(const Material &material);

    /// <summary>
    /// ComputeShaderでスキニング済み頂点を書き込む
    /// </summary>
    void DispatchSkinning(const ModelSubMesh &subMesh);

  private:
    static constexpr uint32_t kMaxDraws = 4096;
    static constexpr size_t kUploadBytesPerFrame = 16 * 1024 * 1024;
    static constexpr size_t kPipelineVariantCount = 12;

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    MeshManager *meshManager_ = nullptr;
    TextureManager *textureManager_ = nullptr;
    MaterialManager *materialManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> shadowRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> skinningRootSignature_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
               kPipelineVariantCount>
        pipelineStates_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
               kPipelineVariantCount>
        instancedPipelineStates_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> instancedShadowPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skinningPSO_;

    UploadRingBuffer uploadBuffer_;
    uint32_t drawIndex_ = 0;
    SceneLighting currentLighting_{};
    SceneFog currentFog_{};
    uint32_t environmentTextureId_ = 0;
    bool hasEnvironmentTexture_ = false;
    D3D12_GPU_DESCRIPTOR_HANDLE shadowMapGpuHandle_{};
    DirectX::XMFLOAT4X4 shadowLightViewProjection_ = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4 shadowParams_{0.0f, 0.0015f, 0.45f, 0.0f};
    DirectX::XMFLOAT4 shadowFilterParams_{1.45f, 2600.0f, 0.045f, 0.0f};
};

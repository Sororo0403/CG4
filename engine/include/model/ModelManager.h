#pragma once
#include "animation/Animator.h"
#include "model/AssimpLoader.h"
#include "model/MaterialManager.h"
#include "model/MeshManager.h"
#include "model/Model.h"
#include "model/ModelRenderer.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class DirectXCommon;
class SrvManager;
class TextureManager;

/// <summary>
/// モデル読み込みと描画関連サブシステムを統括する
/// </summary>
class ModelManager {
  public:
    /// <summary>
    /// ModelManagerの共有インスタンスを取得する
    /// </summary>
    static ModelManager &GetInstance();

    /// <summary>
    /// モデル読み込み、マテリアル、メッシュ、描画器を初期化する
    /// </summary>
    /// <param name="dxCommon">DirectX共通管理クラス</param>
    /// <param name="srvManager">SRVヒープ管理クラス</param>
    /// <param name="textureManager">テクスチャ管理クラス</param>
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    TextureManager *textureManager);

    /// <summary>
    /// 管理中のモデルリソースとキャッシュを明示的に解放する
    /// </summary>
    void Finalize();

    /// <summary>
    /// モデルを読み込む
    /// </summary>
    /// <param name="path">モデルファイルのパス</param>
    /// <returns>モデルID</returns>
    uint32_t Load(const std::wstring &path);

    /// <summary>
    /// XY平面の基本形状を生成する
    /// </summary>
    /// <param name="textureId">貼り付けるテクスチャID</param>
    /// <param name="material">使用するマテリアル</param>
    /// <returns>生成されたモデルID</returns>
    uint32_t CreatePlane(uint32_t textureId, const Material &material);

    /// <summary>
    /// XY平面のリング形状を生成する
    /// </summary>
    /// <param name="textureId">貼り付けるテクスチャID</param>
    /// <param name="material">使用するマテリアル</param>
    /// <param name="divide">分割数(3以上)</param>
    /// <param name="outerRadius">外径半径</param>
    /// <param name="innerRadius">内径半径</param>
    /// <returns>生成されたモデルID</returns>
    uint32_t CreateRing(uint32_t textureId, const Material &material,
                        uint32_t divide = 32, float outerRadius = 1.0f,
                        float innerRadius = 0.2f);

    /// <summary>
    /// Y軸方向に伸びる筒形状を生成する
    /// </summary>
    /// <param name="textureId">貼り付けるテクスチャID</param>
    /// <param name="material">使用するマテリアル</param>
    /// <param name="divide">分割数(3以上)</param>
    /// <param name="topRadius">上面半径</param>
    /// <param name="bottomRadius">下面半径</param>
    /// <param name="height">高さ</param>
    /// <returns>生成されたモデルID</returns>
    uint32_t CreateCylinder(uint32_t textureId, const Material &material,
                            uint32_t divide = 32, float topRadius = 1.0f,
                            float bottomRadius = 1.0f, float height = 3.0f);

    /// <summary>
    /// 頂点配列とインデックス配列から汎用メッシュを作成する
    /// </summary>
    uint32_t CreateMesh(const void *vertexData, uint32_t vertexStride,
                        uint32_t vertexCount, const uint32_t *indexData,
                        uint32_t indexCount,
                        D3D12_PRIMITIVE_TOPOLOGY primitiveTopology =
                            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    /// <summary>
    /// 作成済みメッシュを取得する
    /// </summary>
    const Mesh &GetMesh(uint32_t meshId) const;

    /// <summary>
    /// モデルのアニメーションを更新する
    /// </summary>
    /// <param name="modelId">更新するモデルID</param>
    /// <param name="deltaTime">前フレームからの経過時間(秒)</param>
    void UpdateAnimation(uint32_t modelId, float deltaTime);

    /// <summary>
    /// 指定したアニメーションを再生する
    /// </summary>
    /// <param name="modelId">対象モデルID</param>
    /// <param name="animationName">再生するアニメーション名</param>
    /// <param name="loop">ループ再生するか</param>
    void PlayAnimation(uint32_t modelId, const std::string &animationName,
                       bool loop = true);

    /// <summary>
    /// アニメーションが終了したか判定する
    /// </summary>
    /// <param name="modelId">対象モデルID</param>
    /// <returns>アニメーション終了ならtrue</returns>
    bool IsAnimationFinished(uint32_t modelId) const;

    /// <summary>
    /// モデルデータを取得する
    /// </summary>
    /// <param name="modelId">モデルID</param>
    /// <returns>Modelポインタ</returns>
    Model *GetModel(uint32_t modelId);

    /// <summary>
    /// モデルデータを読み取り専用で取得する
    /// </summary>
    /// <param name="modelId">モデルID</param>
    /// <returns>Modelポインタ</returns>
    const Model *GetModel(uint32_t modelId) const;

    /// <summary>
    /// マテリアル情報を取得する
    /// </summary>
    /// <param name="materialId">マテリアルID</param>
    /// <returns>マテリアル情報</returns>
    const Material &GetMaterial(uint32_t materialId) const;

    /// <summary>
    /// 既存マテリアルを更新する
    /// </summary>
    /// <param name="materialId">更新対象のマテリアルID</param>
    /// <param name="material">設定するマテリアル値</param>
    void SetMaterial(uint32_t materialId, const Material &material);

    /// <summary>
    /// モデルIDから描画する互換ヘルパー
    /// </summary>
    void Draw(uint32_t modelId, const Transform &transform,
              const Camera &camera, uint32_t environmentTextureId = UINT32_MAX);

    /// <summary>
    /// 同一モデルを複数Transformでまとめて描画する
    /// </summary>
    void DrawInstanced(uint32_t modelId, const Transform *transforms,
                       uint32_t instanceCount, const Camera &camera,
                       uint32_t environmentTextureId = UINT32_MAX);

    /// <summary>
    /// 同一モデルを複数InstanceDataでまとめて描画する
    /// </summary>
    void DrawInstanced(uint32_t modelId, const InstanceData *instances,
                       uint32_t instanceCount, const Camera &camera,
                       uint32_t environmentTextureId = UINT32_MAX);

    /// <summary>
    /// モデルIDからShadowMapへ描画する
    /// </summary>
    void DrawShadow(uint32_t modelId, const Transform &transform,
                    const DirectX::XMFLOAT4X4 &lightViewProjection);

    /// <summary>
    /// 同一モデルを複数TransformでShadowMapへまとめて描画する
    /// </summary>
    void DrawInstancedShadow(uint32_t modelId, const Transform *transforms,
                             uint32_t instanceCount,
                             const DirectX::XMFLOAT4X4 &lightViewProjection);

    /// <summary>
    /// 同一モデルを複数InstanceDataでShadowMapへまとめて描画する
    /// </summary>
    void DrawInstancedShadow(uint32_t modelId, const InstanceData *instances,
                             uint32_t instanceCount,
                             const DirectX::XMFLOAT4X4 &lightViewProjection);

    /// <summary>
    /// 毎フレーム変わる描画用Upload領域をリセットする
    /// </summary>
    void BeginFrame() { modelRenderer_.BeginFrame(); }

    /// <summary>
    /// モデル描画用パイプラインを描画前に設定する
    /// </summary>
    void PreDraw() { modelRenderer_.PreDraw(); }

    /// <summary>
    /// ShadowPass用パイプラインを描画前に設定する
    /// </summary>
    void PreDrawShadow() { modelRenderer_.PreDrawShadow(); }

    /// <summary>
    /// モデル描画後の状態を整理する
    /// </summary>
    void PostDraw() { modelRenderer_.PostDraw(); }

    /// <summary>
    /// シーンライティングを設定する
    /// </summary>
    void SetSceneLighting(const SceneLighting &lighting) {
        modelRenderer_.SetSceneLighting(lighting);
    }

    /// <summary>
    /// シーンフォグを設定する
    /// </summary>
    void SetSceneFog(const SceneFog &fog) { modelRenderer_.SetSceneFog(fog); }

    /// <summary>
    /// 描画に使用するModelRendererを取得する
    /// </summary>
    /// <returns>ModelRendererへのポインタ</returns>
    ModelRenderer *GetRenderer() { return &modelRenderer_; }

    /// <summary>
    /// 描画に使用するModelRendererを読み取り専用で取得する
    /// </summary>
    /// <returns>ModelRendererへのポインタ</returns>
    const ModelRenderer *GetRenderer() const { return &modelRenderer_; }

    MeshManager *GetMeshManager() { return &meshManager_; }
    const MeshManager *GetMeshManager() const { return &meshManager_; }

  private:
    DirectXCommon *dxCommon_ = nullptr;
    TextureManager *textureManager_ = nullptr;

    MeshManager meshManager_;
    MaterialManager materialManager_;
    AssimpLoader assimpLoader_;
    ModelRenderer modelRenderer_;
    Animator animator_;

    std::vector<Model> models_;
    std::unordered_map<std::wstring, uint32_t> modelPathToId_;
};

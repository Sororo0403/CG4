#pragma once
#include "Animator.h"
#include "AssimpLoader.h"
#include "MaterialManager.h"
#include "MeshManager.h"
#include "Model.h"
#include "ModelRenderer.h"
#include <string>
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
    /// ModelManagerの初期化
    /// </summary>
    /// <param name="dxCommon">DirectX共通管理クラス</param>
    /// <param name="srvManager">SRVヒープ管理クラス</param>
    /// <param name="textureManager">テクスチャ管理クラス</param>
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    TextureManager *textureManager);

    /// <summary>
    /// モデルを読み込む
    /// </summary>
    /// <param name="path">モデルファイルのパス</param>
    /// <returns>モデルID</returns>
    uint32_t Load(const std::wstring &path);

    /// <summary>
    /// XY平面のPrimitiveを生成する
    /// </summary>
    /// <param name="textureId">貼り付けるテクスチャID</param>
    /// <param name="material">使用するマテリアル</param>
    /// <returns>生成されたモデルID</returns>
    uint32_t CreatePlane(uint32_t textureId, const Material &material);

    /// <summary>
    /// XY平面のRing Primitiveを生成する
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
    /// Y軸方向に伸びる筒状Cylinder Primitiveを生成する(上下キャップなし)
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
    /// 描画に使用するModelRendererを取得する
    /// </summary>
    /// <returns>ModelRendererへのポインタ</returns>
    ModelRenderer *GetRenderer() { return &modelRenderer_; }
    /// <summary>
    /// 描画に使用するModelRendererを読み取り専用で取得する
    /// </summary>
    /// <returns>ModelRendererへのポインタ</returns>
    const ModelRenderer *GetRenderer() const { return &modelRenderer_; }

  private:
    DirectXCommon *dxCommon_ = nullptr;
    TextureManager *textureManager_ = nullptr;

    MeshManager meshManager_;
    MaterialManager materialManager_;
    AssimpLoader assimpLoader_;
    ModelRenderer modelRenderer_;
    Animator animator_;

    std::vector<Model> models_;
};

#pragma once
#include "Animator.h"
#include "AssimpLoader.h"
#include "MaterialManager.h"
#include "MeshManager.h"
#include "Model.h"
#include "ModelRenderer.h"
#include "Transform.h"
#include <string>
#include <vector>

class DirectXCommon;
class SrvManager;
class TextureManager;
class Camera;

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
    /// モデルを描画する
    /// </summary>
    /// <param name="modelId">描画するモデルID</param>
    /// <param name="transform">ワールド変換</param>
    /// <param name="camera">描画に使用するカメラ</param>
    void Draw(uint32_t modelId, const Transform &transform,
              const Camera &camera);

    void SetDrawEffect(const ModelDrawEffect &effect);
    void ClearDrawEffect();
    void SetSceneLighting(const SceneLighting &lighting);

    /// <summary>
    /// モデル描画前の共通処理
    /// </summary>
    void PreDraw();

    /// <summary>
    /// モデル描画後の後処理
    /// </summary>
    void PostDraw();

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

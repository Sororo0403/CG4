#pragma once
#include "animation/Animator.h"
#include "core/ResourceHandle.h"
#include "model/AssimpLoader.h"
#include "model/MaterialManager.h"
#include "model/MeshManager.h"
#include "model/Model.h"
#include "model/ModelRenderer.h"

#include <cstdint>
#include <initializer_list>
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
    ~ModelManager();

    /// <summary>
    /// モデル読み込み、マテリアル、メッシュ、描画器を初期化する
    /// </summary>
    /// <param name="dxCommon">DirectX共通管理クラス</param>
    /// <param name="srvManager">SRVヒープ管理クラス</param>
    /// <param name="textureManager">テクスチャ管理クラス</param>
    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager,
                    TextureManager* textureManager);

    /// <summary>
    /// 管理中のモデルリソースとキャッシュを明示的に解放する
    /// </summary>
    bool Finalize();
    bool Finalize(bool allowFrameAbort);

    /// <summary>
    /// GPU転送完了後にモデル内部メッシュの一時UploadBufferを解放する
    /// </summary>
    void ReleaseUploadBuffers();
    void ReleaseCompletedFrameResources();

    /// <summary>
    /// モデルを読み込む
    /// </summary>
    /// <param name="path">モデルファイルのパス</param>
    /// <returns>モデルID</returns>
    uint32_t Load(const std::wstring& path);
    ModelHandle LoadHandle(const std::wstring& path);

    /// <summary>
    /// XY平面の基本形状を生成する
    /// </summary>
    /// <param name="textureId">貼り付けるテクスチャID</param>
    /// <param name="material">使用するマテリアル</param>
    /// <returns>生成されたモデルID</returns>
    /// <summary>
    /// Planeを生成する
    /// </summary>
    uint32_t CreatePlane(uint32_t textureId, const Material& material);
    ModelHandle CreatePlaneHandle(uint32_t textureId, const Material& material);
    ModelHandle CreatePlaneHandle(TextureHandle texture, const Material& material);

    /// <summary>
    /// Y軸方向に伸びる直方体Primitiveを生成する
    /// </summary>
    uint32_t CreateBox(uint32_t textureId, const Material& material, float width = 1.0f,
                       float height = 1.0f, float depth = 1.0f);
    ModelHandle CreateBoxHandle(uint32_t textureId, const Material& material, float width = 1.0f,
                                float height = 1.0f, float depth = 1.0f);
    ModelHandle CreateBoxHandle(TextureHandle texture, const Material& material, float width = 1.0f,
                                float height = 1.0f, float depth = 1.0f);

    /// <summary>
    /// 球体Primitiveを生成する
    /// </summary>
    uint32_t CreateSphere(uint32_t textureId, const Material& material, uint32_t slice = 24,
                          uint32_t stack = 12, float radius = 1.0f);
    ModelHandle CreateSphereHandle(uint32_t textureId, const Material& material,
                                   uint32_t slice = 24, uint32_t stack = 12, float radius = 1.0f);
    ModelHandle CreateSphereHandle(TextureHandle texture, const Material& material,
                                   uint32_t slice = 24, uint32_t stack = 12, float radius = 1.0f);

    /// <summary>
    /// XY平面のリング形状を生成する
    /// </summary>
    /// <param name="textureId">貼り付けるテクスチャID</param>
    /// <param name="material">使用するマテリアル</param>
    /// <param name="divide">分割数(3以上)</param>
    /// <param name="outerRadius">外径半径</param>
    /// <param name="innerRadius">内径半径</param>
    /// <returns>生成されたモデルID</returns>
    uint32_t CreateRing(uint32_t textureId, const Material& material, uint32_t divide = 32,
                        float outerRadius = 1.0f, float innerRadius = 0.2f);
    ModelHandle CreateRingHandle(uint32_t textureId, const Material& material, uint32_t divide = 32,
                                 float outerRadius = 1.0f, float innerRadius = 0.2f);
    ModelHandle CreateRingHandle(TextureHandle texture, const Material& material,
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
    uint32_t CreateCylinder(uint32_t textureId, const Material& material, uint32_t divide = 32,
                            float topRadius = 1.0f, float bottomRadius = 1.0f, float height = 3.0f);
    ModelHandle CreateCylinderHandle(uint32_t textureId, const Material& material,
                                     uint32_t divide = 32, float topRadius = 1.0f,
                                     float bottomRadius = 1.0f, float height = 3.0f);
    ModelHandle CreateCylinderHandle(TextureHandle texture, const Material& material,
                                     uint32_t divide = 32, float topRadius = 1.0f,
                                     float bottomRadius = 1.0f, float height = 3.0f);

    /// <summary>
    /// 頂点配列とインデックス配列から汎用メッシュを作成する
    /// </summary>
    uint32_t CreateMesh(
        const void* vertexData, uint32_t vertexStride, uint32_t vertexCount,
        const uint32_t* indexData, uint32_t indexCount,
        D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    MeshHandle CreateMeshHandle(
        const void* vertexData, uint32_t vertexStride, uint32_t vertexCount,
        const uint32_t* indexData, uint32_t indexCount,
        D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    /// <summary>
    /// 作成済みメッシュを取得する
    /// </summary>
    const Mesh& GetMesh(uint32_t meshId) const;
    const Mesh& GetMesh(MeshHandle meshId) const;

    /// <summary>
    /// モデルのアニメーションを更新する
    /// </summary>
    /// <param name="modelId">更新するモデルID</param>
    /// <param name="deltaTime">前フレームからの経過時間(秒)</param>
    void UpdateAnimation(uint32_t modelId, float deltaTime);
    void UpdateAnimation(ModelHandle modelId, float deltaTime);

    /// <summary>
    /// 指定したアニメーションを再生する
    /// </summary>
    /// <param name="modelId">対象モデルID</param>
    /// <param name="animationName">再生するアニメーション名</param>
    /// <param name="loop">ループ再生するか</param>
    void PlayAnimation(uint32_t modelId, const std::string& animationName, bool loop = true);
    void PlayAnimation(ModelHandle modelId, const std::string& animationName, bool loop = true);

    /// <summary>
    /// アニメーションが終了したか判定する
    /// </summary>
    /// <param name="modelId">対象モデルID</param>
    /// <returns>アニメーション終了ならtrue</returns>
    bool IsAnimationFinished(uint32_t modelId) const;
    bool IsAnimationFinished(ModelHandle modelId) const;

    /// <summary>
    /// モデルデータを取得する
    /// </summary>
    /// <param name="modelId">モデルID</param>
    /// <returns>Modelポインタ</returns>
    Model* GetModel(uint32_t modelId);
    Model* GetModel(ModelHandle modelId);

    /// <summary>
    /// モデルデータを読み取り専用で取得する
    /// </summary>
    /// <param name="modelId">モデルID</param>
    /// <returns>Modelポインタ</returns>
    const Model* GetModel(uint32_t modelId) const;
    const Model* GetModel(ModelHandle modelId) const;

    /// <summary>
    /// マテリアル情報を取得する
    /// </summary>
    /// <param name="materialId">マテリアルID</param>
    /// <returns>マテリアル情報</returns>
    const Material& GetMaterial(uint32_t materialId) const;
    const Material& GetMaterial(MaterialHandle materialId) const;

    /// <summary>
    /// 既存マテリアルを更新する
    /// </summary>
    /// <param name="materialId">更新対象のマテリアルID</param>
    /// <param name="material">設定するマテリアル値</param>
    void SetMaterial(uint32_t materialId, const Material& material);
    void SetMaterial(MaterialHandle materialId, const Material& material);

    /// <summary>
    /// モデルIDから描画する互換ヘルパー
    /// </summary>
    void Draw(uint32_t modelId, const Transform& transform, const Camera& camera,
              uint32_t environmentTextureId = kInvalidResourceId);
    void Draw(ModelHandle modelId, const Transform& transform, const Camera& camera,
              TextureHandle environmentTexture = TextureHandle());

    /// <summary>
    /// 同一モデルを複数Transformでまとめて描画する
    /// </summary>
    void DrawInstanced(uint32_t modelId, const Transform* transforms, uint32_t instanceCount,
                       const Camera& camera, uint32_t environmentTextureId = kInvalidResourceId);
    void DrawInstanced(ModelHandle modelId, const Transform* transforms, uint32_t instanceCount,
                       const Camera& camera, TextureHandle environmentTexture = TextureHandle());

    /// <summary>
    /// 同一モデルを複数InstanceDataでまとめて描画する
    /// </summary>
    void DrawInstanced(uint32_t modelId, const InstanceData* instances, uint32_t instanceCount,
                       const Camera& camera, uint32_t environmentTextureId = kInvalidResourceId);
    void DrawInstanced(ModelHandle modelId, const InstanceData* instances, uint32_t instanceCount,
                       const Camera& camera, TextureHandle environmentTexture = TextureHandle());

    /// <summary>
    /// モデルIDからShadowMapへ描画する
    /// </summary>
    void DrawShadow(uint32_t modelId, const Transform& transform,
                    const DirectX::XMFLOAT4X4& lightViewProjection);
    void DrawShadow(ModelHandle modelId, const Transform& transform,
                    const DirectX::XMFLOAT4X4& lightViewProjection);

    /// <summary>
    /// 同一モデルを複数TransformでShadowMapへまとめて描画する
    /// </summary>
    void DrawInstancedShadow(uint32_t modelId, const Transform* transforms, uint32_t instanceCount,
                             const DirectX::XMFLOAT4X4& lightViewProjection);
    void DrawInstancedShadow(ModelHandle modelId, const Transform* transforms,
                             uint32_t instanceCount,
                             const DirectX::XMFLOAT4X4& lightViewProjection);

    /// <summary>
    /// 同一モデルを複数InstanceDataでShadowMapへまとめて描画する
    /// </summary>
    void DrawInstancedShadow(uint32_t modelId, const InstanceData* instances,
                             uint32_t instanceCount,
                             const DirectX::XMFLOAT4X4& lightViewProjection);
    void DrawInstancedShadow(ModelHandle modelId, const InstanceData* instances,
                             uint32_t instanceCount,
                             const DirectX::XMFLOAT4X4& lightViewProjection);

    /// <summary>
    /// モデルIDから描画前のGPUスキニングだけを実行する
    /// </summary>
    void PrepareSkinning(uint32_t modelId);
    void PrepareSkinning(ModelHandle modelId);
    void PrepareSkinning(std::initializer_list<uint32_t> modelIds);
    void PrepareSkinningHandles(std::initializer_list<ModelHandle> modelIds);

    /// <summary>
    /// 毎フレーム変わる描画用Upload領域をリセットする
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// モデル描画用パイプラインを描画前に設定する
    /// </summary>
    void PreDraw();

    /// <summary>
    /// ShadowPass用パイプラインを描画前に設定する
    /// </summary>
    void PreDrawShadow();

    /// <summary>
    /// モデル描画後の状態を整理する
    /// </summary>
    void PostDraw();

    /// <summary>
    /// シーンライティングを設定する
    /// </summary>
    void SetSceneLighting(const SceneLighting& lighting);

    /// <summary>
    /// 現在フレームの描画エフェクトを設定する
    /// </summary>
    void SetDrawEffect(const ModelDrawEffect& effect);

    /// <summary>
    /// 描画エフェクトを初期状態へ戻す
    /// </summary>
    void ClearDrawEffect();

    /// <summary>
    /// シーンフォグを設定する
    /// </summary>
    void SetSceneFog(const SceneFog& fog);

    /// <summary>
    /// 描画に使用するModelRendererを取得する
    /// </summary>
    /// <returns>ModelRendererへのポインタ</returns>
    ModelRenderer* GetRenderer();

    /// <summary>
    /// 描画に使用するModelRendererを読み取り専用で取得する
    /// </summary>
    /// <returns>ModelRendererへのポインタ</returns>
    const ModelRenderer* GetRenderer() const;

    MeshManager* GetMeshManager();
    const MeshManager* GetMeshManager() const;
    bool IsReady() const;

private:
    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    TextureManager* textureManager_ = nullptr;

    MeshManager meshManager_;
    MaterialManager materialManager_;
    AssimpLoader assimpLoader_;
    ModelRenderer modelRenderer_;
    Animator animator_;

    std::vector<Model> models_;
    std::unordered_map<std::wstring, uint32_t> modelPathToId_;
};

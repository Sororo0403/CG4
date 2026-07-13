#pragma once
#include "camera/Camera.h"

#include <cstdint>
#include <memory>

class DirectXCommon;
class TextureManager;
class SrvManager;

/// <summary>
/// キューブマップを使ったスカイボックスを描画する
/// </summary>
class SkyboxRenderer {
public:
    SkyboxRenderer();
    ~SkyboxRenderer();

    /// <summary>
    /// スカイボックス描画に必要なリソースを初期化する
    /// </summary>
    /// <param name="dxCommon">DirectX共通管理</param>
    /// <param name="srvManager">SRV管理</param>
    /// <param name="textureManager">テクスチャ管理</param>
    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager,
                    TextureManager* textureManager);

    /// <summary>
    /// スカイボックス描画用GPUリソースを解放する
    /// </summary>
    bool Finalize();
    bool Finalize(bool allowFrameAbort);

    /// <summary>
    /// スカイボックスを描画する
    /// </summary>
    /// <param
    /// name="textureId">描画に使用するキューブマップのテクスチャID</param>
    /// <param name="camera">描画に使用するカメラ</param>
    /// <summary>
    /// 描画を実行する
    /// </summary>
    void Draw(uint32_t textureId, const Camera& camera);
    bool IsReady() const;

private:
    /// <summary>
    /// ルートシグネチャを生成する
    /// </summary>
    void CreateRootSignature();

    /// <summary>
    /// パイプラインステートを生成する
    /// </summary>
    void CreatePipelineState();

    /// <summary>
    /// スカイボックス描画用メッシュを生成する
    /// </summary>
    void CreateMesh();

    /// <summary>
    /// 定数バッファを生成する
    /// </summary>
    void CreateConstantBuffer();

private:
    struct ConstBufferData;
    struct ConstantFrame;
    struct State;

    ConstantFrame* GetCurrentConstantFrame();
    const ConstantFrame* GetCurrentConstantFrame() const;
    bool HasConstantBuffers() const;

    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    TextureManager* textureManager_ = nullptr;
    std::unique_ptr<State> state_;
};

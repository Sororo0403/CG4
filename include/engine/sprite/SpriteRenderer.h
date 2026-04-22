#pragma once
#include "Sprite.h"
#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;
class TextureManager;
class SrvManager;

/// <summary>
/// 2Dスプライト描画パイプラインを管理する
/// </summary>
class SpriteRenderer {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="textureManager">TextureManagerインスタンス</param>
    /// <param name="srvManager">SrvManagerインスタンス</param>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    void Initialize(DirectXCommon *dxCommon, TextureManager *textureManager,
                    SrvManager *srvManager, int width, int height);

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="sprite">描画するスプライト</param>
    void Draw(const Sprite &sprite);

    /// <summary>
    /// 描画前処理
    /// </summary>
    void PreDraw();

    /// <summary>
    /// 描画後処理
    /// </summary>
    void PostDraw();

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
    /// 頂点バッファを生成する
    /// </summary>
    void CreateVertexBuffer();
    /// <summary>
    /// 定数バッファを生成する
    /// </summary>
    void CreateConstantBuffer();

    /// <summary>
    /// 投影行列を更新する
    /// </summary>
    /// <param name="width">描画領域の幅</param>
    /// <param name="height">描画領域の高さ</param>
    void UpdateProjection(int width, int height);

  private:
    DirectXCommon *dxCommon_ = nullptr;
    TextureManager *textureManager_ = nullptr;
    SrvManager *srvManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vbView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;

    DirectX::XMFLOAT4X4 matProjection_{};
};

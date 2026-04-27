#pragma once
#include "Camera.h"
#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;
class TextureManager;
class SrvManager;

/// <summary>
/// キューブマップを使ったスカイボックスを描画する
/// </summary>
class SkyboxRenderer {
  public:
    /// <summary>
    /// スカイボックス描画に必要なリソースを初期化する
    /// </summary>
    /// <param name="dxCommon">DirectX共通管理</param>
    /// <param name="srvManager">SRV管理</param>
    /// <param name="textureManager">テクスチャ管理</param>
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    TextureManager *textureManager);

    /// <summary>
    /// スカイボックスを描画する
    /// </summary>
    /// <param name="textureId">描画に使用するキューブマップのテクスチャID</param>
    /// <param name="camera">描画に使用するカメラ</param>
    void Draw(uint32_t textureId, const Camera &camera);

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
    struct ConstBufferData {
        DirectX::XMFLOAT4X4 matWVP{};
    };

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    TextureManager *textureManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;

    D3D12_VERTEX_BUFFER_VIEW vbView_{};
    D3D12_INDEX_BUFFER_VIEW ibView_{};

    ConstBufferData *mappedCB_ = nullptr;
    uint32_t indexCount_ = 0;
    bool hasCachedCameraState_ = false;
    DirectX::XMFLOAT3 cachedCameraPosition_ = {};
    DirectX::XMFLOAT4X4 cachedView_ = {};
    DirectX::XMFLOAT4X4 cachedProj_ = {};
};

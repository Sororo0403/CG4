#pragma once
#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;
class SrvManager;

/// <summary>
/// 描画先として使い、あとからシェーダーで読むためのテクスチャ
/// </summary>
class RenderTexture {
  public:
    /// <summary>
    /// RenderTextureを生成する
    /// </summary>
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager, int width,
                    int height);

    /// <summary>
    /// サイズ変更に合わせて内部リソースを再生成する
    /// </summary>
    void Resize(int width, int height);

    /// <summary>
    /// RenderTextureへの描画を開始する
    /// </summary>
    void BeginRender(const DirectX::XMFLOAT4 &clearColor);

    /// <summary>
    /// RenderTextureへの描画を終了し、シェーダーから読める状態にする
    /// </summary>
    void EndRender();

    /// <summary>
    /// SRVのGPUハンドルを取得する
    /// </summary>
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const;

    /// <summary>
    /// テクスチャ幅を取得する
    /// </summary>
    int GetWidth() const { return width_; }

    /// <summary>
    /// テクスチャ高さを取得する
    /// </summary>
    int GetHeight() const { return height_; }

  private:
    /// <summary>
    /// 描画先リソースとビューを生成する
    /// </summary>
    void CreateResources();

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    UINT rtvDescriptorSize_ = 0;
    UINT srvIndex_ = 0;
    int width_ = 0;
    int height_ = 0;
};

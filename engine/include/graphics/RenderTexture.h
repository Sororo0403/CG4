#pragma once
#include <DirectXMath.h>
#include <d3d12.h>
#include <memory>

class DirectXCommon;
class SrvManager;

/// <summary>
/// 描画先として使い、あとからシェーダーで読むためのテクスチャ
/// </summary>
class RenderTexture {
public:
    RenderTexture();
    ~RenderTexture();

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, int width, int height);

    /// <summary>
    /// サイズ変更に合わせて内部リソースを再生成する
    /// </summary>
    bool Resize(int width, int height);

    /// <summary>
    /// 内部リソースとSRV割り当てを解放する
    /// </summary>
    bool Release();
    bool Release(bool allowFrameAbort);

    /// <summary>
    /// RenderTextureへの描画を開始する
    /// </summary>
    void BeginRender(const DirectX::XMFLOAT4& clearColor);

    /// <summary>
    /// Depth bufferをbindせずにRenderTextureへの描画を開始する
    /// </summary>
    void BeginRenderNoDepth(const DirectX::XMFLOAT4& clearColor);

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
    int GetWidth() const;

    /// <summary>
    /// テクスチャ高さを取得する
    /// </summary>
    int GetHeight() const;
    bool IsReady() const;

private:
    /// <summary>
    /// 描画先リソースとビューを生成する
    /// </summary>
    bool CreateResources();
    bool ReleaseTextureResources();
    bool ReleaseTextureResources(bool allowFrameAbort);
    void BeginRenderInternal(const DirectX::XMFLOAT4& clearColor, bool bindDepth, bool clearDepth);

    struct State;

    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    std::unique_ptr<State> resources_;
};

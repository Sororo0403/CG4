#pragma once
#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;
class SrvManager;

class ShadowMapRenderer {
  public:
    ~ShadowMapRenderer();

    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    uint32_t width = 2048, uint32_t height = 2048);
    /// <summary>
    /// Releaseを実行する
    /// </summary>
    bool Release();
    bool Release(bool allowFrameAbort);
    bool Resize(uint32_t width, uint32_t height);

    void Begin();
    /// <summary>
    /// Endを実行する
    /// </summary>
    void End();

    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const;
    /// <summary>
    /// DsvHandleを取得する
    /// </summary>
    D3D12_CPU_DESCRIPTOR_HANDLE GetDsvHandle() const;
    const DirectX::XMFLOAT4X4 &GetLightViewProjection() const {
        return lightViewProjection_;
    }

    void SetLightViewProjection(const DirectX::XMFLOAT4X4 &matrix) {
        lightViewProjection_ = matrix;
    }

    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }
    bool IsReady() const {
        return dxCommon_ != nullptr && srvManager_ != nullptr &&
               depthTexture_ && dsvHeap_ && srvIndex_ != UINT32_MAX &&
               srvGpuHandle_.ptr != 0;
    }

  private:
    /// <summary>
    /// DepthResourcesを解放する
    /// </summary>
    bool ReleaseDepthResources();
    bool ReleaseDepthResources(bool allowFrameAbort);
    bool CreateResources();
    bool UpdateSrv();

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthTexture_;
    uint32_t width_ = 2048;
    uint32_t height_ = 2048;
    uint32_t srvIndex_ = UINT32_MAX;
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle_{};
    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissor_{};
    D3D12_RESOURCE_STATES state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    DirectX::XMFLOAT4X4 lightViewProjection_ = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
};

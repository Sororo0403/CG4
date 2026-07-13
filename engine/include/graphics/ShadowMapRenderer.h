#pragma once
#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <memory>

class DirectXCommon;
class SrvManager;

class ShadowMapRenderer {
public:
    ShadowMapRenderer();
    ~ShadowMapRenderer();

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width = 2048,
                    uint32_t height = 2048);
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
    const DirectX::XMFLOAT4X4& GetLightViewProjection() const;

    void SetLightViewProjection(const DirectX::XMFLOAT4X4& matrix);

    uint32_t GetWidth() const;
    uint32_t GetHeight() const;
    bool IsReady() const;

private:
    /// <summary>
    /// DepthResourcesを解放する
    /// </summary>
    bool ReleaseDepthResources();
    bool ReleaseDepthResources(bool allowFrameAbort);
    bool CreateResources();
    bool UpdateSrv();

    struct State;

    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    std::unique_ptr<State> resources_;
};

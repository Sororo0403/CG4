#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl.h>

class DirectXCommon;
class SrvManager;

class DepthPyramid {
  public:
    static constexpr uint32_t kMaxMipCount = 12u;

    ~DepthPyramid();

    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    uint32_t width, uint32_t height);
    bool Release();
    bool Release(bool allowFrameAbort);
    bool Resize(uint32_t width, uint32_t height);

    bool Build(D3D12_GPU_DESCRIPTOR_HANDLE sceneDepth);

    bool IsReady() const {
        return dxCommon_ != nullptr && srvManager_ != nullptr && resource_ &&
               rootSignature_ && pipelineState_ && srvGpuHandle_.ptr != 0 &&
               mipCount_ > 0;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const { return srvGpuHandle_; }
    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }
    uint32_t GetMipCount() const { return mipCount_; }

  private:
    struct BuildConstants {
        uint32_t sourceWidth = 1;
        uint32_t sourceHeight = 1;
        uint32_t targetWidth = 1;
        uint32_t targetHeight = 1;
        uint32_t sourceMip = 0;
        uint32_t padding[3]{};
    };

    bool CreatePipeline();
    bool CreateResources(uint32_t width, uint32_t height);
    bool ReleaseResources();
    bool ReleaseResources(bool allowFrameAbort);
    bool AllocateDescriptors(uint32_t mipCount);
    void FreeDescriptorRange(uint32_t start, uint32_t count);
    void FreeDescriptors();
    bool TransitionSubresource(uint32_t mip, D3D12_RESOURCE_STATES state);

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    std::vector<D3D12_RESOURCE_STATES> subresourceStates_;
    uint32_t descriptorStart_ = UINT32_MAX;
    uint32_t descriptorCount_ = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle_{};
    uint32_t sourceWidth_ = 1;
    uint32_t sourceHeight_ = 1;
    uint32_t width_ = 1;
    uint32_t height_ = 1;
    uint32_t mipCount_ = 0;
};

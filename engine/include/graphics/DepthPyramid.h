#pragma once

#include <cstdint>
#include <d3d12.h>
#include <memory>

class DirectXCommon;
class SrvManager;

class DepthPyramid {
public:
    static constexpr uint32_t kMaxMipCount = 12u;

    DepthPyramid();
    ~DepthPyramid();

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width,
                    uint32_t height);
    bool Release();
    bool Release(bool allowFrameAbort);
    bool Resize(uint32_t width, uint32_t height);

    bool Build(D3D12_GPU_DESCRIPTOR_HANDLE sceneDepth);

    bool IsReady() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const;
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;
    uint32_t GetMipCount() const;

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
    bool ValidateBuildDescriptors() const;
    void BindBuildPipeline(ID3D12GraphicsCommandList* commandList,
                           ID3D12DescriptorHeap* heap) const;
    bool DispatchBuildMip(ID3D12GraphicsCommandList* commandList,
                          D3D12_GPU_DESCRIPTOR_HANDLE sceneDepth, uint32_t mip,
                          uint32_t& sourceWidth, uint32_t& sourceHeight, uint32_t& targetWidth,
                          uint32_t& targetHeight);
    void FreeDescriptorRange(uint32_t start, uint32_t count);
    void FreeDescriptors();
    bool TransitionSubresource(uint32_t mip, D3D12_RESOURCE_STATES state);

    struct State;

    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    std::unique_ptr<State> resources_;
};

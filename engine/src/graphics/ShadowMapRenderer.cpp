#include "graphics/ShadowMapRenderer.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/SrvManager.h"
#include <algorithm>
#include <limits>

namespace {
bool CreateCommittedResourceChecked(
    ID3D12Device *device, const D3D12_HEAP_PROPERTIES *heapProperties,
    D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *resourceDesc,
    D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *clearValue,
    ID3D12Resource **resource) {
    if (device == nullptr || heapProperties == nullptr ||
        resourceDesc == nullptr || resource == nullptr) {
        return false;
    }
    *resource = nullptr;
    return SUCCEEDED(device->CreateCommittedResource(
        heapProperties, heapFlags, resourceDesc, initialState, clearValue,
        IID_PPV_ARGS(resource))) &&
           *resource != nullptr;
}

class ShadowMapInitializationGuard {
  public:
    explicit ShadowMapInitializationGuard(ShadowMapRenderer &target)
        : target_(target) {}
    ~ShadowMapInitializationGuard() {
        if (active_) {
            target_.Release();
        }
    }

    ShadowMapInitializationGuard(const ShadowMapInitializationGuard &) = delete;
    ShadowMapInitializationGuard &
    operator=(const ShadowMapInitializationGuard &) = delete;

    void Commit() { active_ = false; }

  private:
    ShadowMapRenderer &target_;
    bool active_ = true;
};
} // namespace

ShadowMapRenderer::~ShadowMapRenderer() {
    Release();
}

void ShadowMapRenderer::Initialize(DirectXCommon *dxCommon,
                                   SrvManager *srvManager, uint32_t width,
                                   uint32_t height) {
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager) {
        Release();
        return;
    }

    Release();

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    ShadowMapInitializationGuard initializeGuard(*this);
    if (!srvManager_->CanAllocate()) {
        return;
    }
    srvIndex_ = srvManager_->Allocate();
    if (srvIndex_ == UINT32_MAX) {
        return;
    }
    Resize(width, height);
    if (!depthTexture_ || !dsvHeap_ || srvGpuHandle_.ptr == 0) {
        return;
    }
    initializeGuard.Commit();
}

void ShadowMapRenderer::Release() {
    ReleaseDepthResources();

    if (srvManager_ != nullptr && srvIndex_ != UINT32_MAX) {
        srvManager_->FreeIfAllocated(srvIndex_);
    }

    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    srvIndex_ = UINT32_MAX;
    srvGpuHandle_ = {};
    state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void ShadowMapRenderer::Resize(uint32_t width, uint32_t height) {
    if (!dxCommon_ || !srvManager_ || srvIndex_ == UINT32_MAX) {
        return;
    }

    constexpr uint32_t kMaxShadowMapSize =
        static_cast<uint32_t>((std::numeric_limits<LONG>::max)());
    width_ = std::clamp(width, 1u, kMaxShadowMapSize);
    height_ = std::clamp(height, 1u, kMaxShadowMapSize);

    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.Width = static_cast<float>(width_);
    viewport_.Height = static_cast<float>(height_);
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;

    scissor_.left = 0;
    scissor_.top = 0;
    scissor_.right = static_cast<LONG>(width_);
    scissor_.bottom = static_cast<LONG>(height_);

    ReleaseDepthResources();
    CreateResources();
    UpdateSrv();
}

void ShadowMapRenderer::Begin() {
    if (!dxCommon_ || !depthTexture_ || !dsvHeap_) {
        return;
    }

    auto commandList = dxCommon_->GetCommandList();
    if (commandList == nullptr || GetDsvHandle().ptr == 0) {
        return;
    }

    if (state_ != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            depthTexture_.Get(), state_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        commandList->ResourceBarrier(1, &barrier);
        state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = GetDsvHandle();
    commandList->RSSetViewports(1, &viewport_);
    commandList->RSSetScissorRects(1, &scissor_);
    commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0,
                                       nullptr);
}

void ShadowMapRenderer::End() {
    if (!dxCommon_ || !depthTexture_) {
        return;
    }

    auto commandList = dxCommon_->GetCommandList();
    if (commandList == nullptr) {
        return;
    }

    if (state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            depthTexture_.Get(), state_,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commandList->ResourceBarrier(1, &barrier);
        state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
}

D3D12_GPU_DESCRIPTOR_HANDLE ShadowMapRenderer::GetGpuHandle() const {
    if (!depthTexture_ || srvIndex_ == UINT32_MAX || srvGpuHandle_.ptr == 0) {
        return {};
    }

    return srvGpuHandle_;
}

D3D12_CPU_DESCRIPTOR_HANDLE ShadowMapRenderer::GetDsvHandle() const {
    if (!dsvHeap_) {
        return {};
    }

    return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
}

void ShadowMapRenderer::ReleaseDepthResources() {
    if (depthTexture_ && dxCommon_ != nullptr &&
        !dxCommon_->IsDeviceRemoved() &&
        !dxCommon_->IsCommandListRecording()) {
        dxCommon_->WaitForGpuIfPossible();
    }

    depthTexture_.Reset();
    dsvHeap_.Reset();
    srvGpuHandle_ = {};
    state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void ShadowMapRenderer::CreateResources() {
    auto device = dxCommon_->GetDevice();
    if (device == nullptr) {
        return;
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    if (FAILED(device->CreateDescriptorHeap(&dsvHeapDesc,
                                            IID_PPV_ARGS(&dsvHeap_))) ||
        !dsvHeap_) {
        return;
    }

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width_;
    desc.Height = height_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    if (!CreateCommittedResourceChecked(
            device, &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
            depthTexture_.GetAddressOf())) {
        return;
    }
    state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetDsvHandle();
    if (dsvHandle.ptr == 0) {
        return;
    }
    device->CreateDepthStencilView(depthTexture_.Get(), &dsvDesc, dsvHandle);
}

void ShadowMapRenderer::UpdateSrv() {
    if (!dxCommon_ || !dxCommon_->GetDevice() || !srvManager_ ||
        srvIndex_ == UINT32_MAX || !depthTexture_) {
        srvGpuHandle_ = {};
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    const D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
        srvManager_->GetCpuHandle(srvIndex_);
    if (srvHandle.ptr == 0) {
        srvGpuHandle_ = {};
        return;
    }
    dxCommon_->GetDevice()->CreateShaderResourceView(depthTexture_.Get(),
                                                     &srvDesc, srvHandle);
    srvGpuHandle_ = srvManager_->GetGpuHandle(srvIndex_);
}

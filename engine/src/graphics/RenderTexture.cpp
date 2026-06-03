#include "graphics/RenderTexture.h"
#include "core/Numeric.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;
using Numeric::ClampFinite;

class RenderTextureInitializationGuard {
  public:
    explicit RenderTextureInitializationGuard(RenderTexture &target)
        : target_(target) {}
    ~RenderTextureInitializationGuard() {
        if (active_) {
            target_.Release();
        }
    }

    RenderTextureInitializationGuard(const RenderTextureInitializationGuard &) =
        delete;
    RenderTextureInitializationGuard &
    operator=(const RenderTextureInitializationGuard &) = delete;

    void Commit() { active_ = false; }

  private:
    RenderTexture &target_;
    bool active_ = true;
};
} // namespace

RenderTexture::~RenderTexture() {
    Release(true);
}

void RenderTexture::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                               int width, int height) {
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager) {
        Release();
        return;
    }
    if (width <= 0 || height <= 0) {
        Release();
        return;
    }

    if (!Release()) {
        return;
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    RenderTextureInitializationGuard initializeGuard(*this);
    if (!srvManager_->CanAllocate()) {
        return;
    }
    srvIndex_ = srvManager_->Allocate();
    if (srvIndex_ == UINT_MAX) {
        return;
    }
    width_ = width;
    height_ = height;

    if (!CreateResources() || !resource_ || !rtvHeap_ ||
        GetGpuHandle().ptr == 0) {
        return;
    }
    initializeGuard.Commit();
}

void RenderTexture::Resize(int width, int height) {
    if (width <= 0 || height <= 0 ||
        (width == width_ && height == height_ && resource_ && rtvHeap_)) {
        return;
    }
    if (!dxCommon_ || !srvManager_ || srvIndex_ == UINT_MAX) {
        return;
    }
    if (dxCommon_->IsCommandListRecording()) {
        return;
    }

    const int previousWidth = width_;
    const int previousHeight = height_;
    width_ = width;
    height_ = height;

    if (!CreateResources()) {
        width_ = previousWidth;
        height_ = previousHeight;
    }
}

bool RenderTexture::Release() { return Release(false); }

bool RenderTexture::Release(bool allowFrameAbort) {
    if (!ReleaseTextureResources(allowFrameAbort)) {
        return false;
    }
    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }

    if (srvManager_ != nullptr && srvIndex_ != UINT_MAX) {
        srvManager_->FreeIfAllocated(srvIndex_);
        srvIndex_ = UINT_MAX;
    }

    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    width_ = 0;
    height_ = 0;
    return true;
}

void RenderTexture::BeginRender(const DirectX::XMFLOAT4 &clearColor) {
    if (!dxCommon_ || !resource_ || !rtvHeap_) {
        return;
    }

    auto commandList = dxCommon_->GetCommandList();
    auto dsvHandle = dxCommon_->GetDepthStencilView();
    if (commandList == nullptr || dsvHandle.ptr == 0) {
        return;
    }

    if (resourceState_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        const D3D12_RESOURCE_STATES previousState = resourceState_;
        if (!dxCommon_->RegisterFrameRollback(
                this,
                [this, previousState]() { resourceState_ = previousState; })) {
            return;
        }
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            resource_.Get(), resourceState_,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandList->ResourceBarrier(1, &barrier);
        resourceState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    D3D12_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(width_);
    viewport.Height = static_cast<float>(height_);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect{};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = width_;
    scissorRect.bottom = height_;

    auto rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    const float clear[4] = {
        ClampFinite(clearColor.x, 0.0f, 1.0f, 0.0f),
        ClampFinite(clearColor.y, 0.0f, 1.0f, 0.0f),
        ClampFinite(clearColor.z, 0.0f, 1.0f, 0.0f),
        ClampFinite(clearColor.w, 0.0f, 1.0f, 1.0f),
    };
    commandList->ClearRenderTargetView(rtvHandle, clear, 0, nullptr);
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f,
                                       0, 0, nullptr);
}

void RenderTexture::EndRender() {
    if (!dxCommon_ || !resource_) {
        return;
    }

    if (resourceState_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        auto commandList = dxCommon_->GetCommandList();
        if (commandList == nullptr) {
            return;
        }
        const D3D12_RESOURCE_STATES previousState = resourceState_;
        if (!dxCommon_->RegisterFrameRollback(
                this,
                [this, previousState]() { resourceState_ = previousState; })) {
            return;
        }
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            resource_.Get(), resourceState_,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commandList->ResourceBarrier(1, &barrier);
        resourceState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
}

D3D12_GPU_DESCRIPTOR_HANDLE RenderTexture::GetGpuHandle() const {
    if (!resource_ || !srvManager_ || srvIndex_ == UINT_MAX) {
        return {};
    }

    return srvManager_->GetGpuHandle(srvIndex_);
}

bool RenderTexture::ReleaseTextureResources() {
    return ReleaseTextureResources(false);
}

bool RenderTexture::ReleaseTextureResources(bool allowFrameAbort) {
    if (!CanReleaseGpuResources(dxCommon_, resource_ != nullptr || rtvHeap_ ||
                                               srvIndex_ != UINT_MAX,
                                allowFrameAbort)) {
        return false;
    }

    resource_.Reset();
    rtvHeap_.Reset();
    rtvDescriptorSize_ = 0;
    resourceState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    return true;
}

bool RenderTexture::CreateResources() {
    auto device = dxCommon_->GetDevice();
    if (device == nullptr || srvManager_ == nullptr || srvIndex_ == UINT_MAX) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> newRtvHeap;
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc,
                                            IID_PPV_ARGS(&newRtvHeap))) ||
        !newRtvHeap) {
        return false;
    }

    const UINT newRtvDescriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    auto resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DirectXCommon::kSceneColorFormat, static_cast<UINT64>(width_),
        static_cast<UINT>(height_), 1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DirectXCommon::kSceneColorFormat;
    clearValue.Color[0] = 0.1f;
    clearValue.Color[1] = 0.2f;
    clearValue.Color[2] = 0.4f;
    clearValue.Color[3] = 1.0f;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    Microsoft::WRL::ComPtr<ID3D12Resource> newResource;
    if (!CreateCommittedResourceChecked(
            device, &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
            newResource.GetAddressOf())) {
        return false;
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DirectXCommon::kSceneColorFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(
        newResource.Get(), &rtvDesc,
        newRtvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DirectXCommon::kSceneColorFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    const D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
        srvManager_->GetCpuHandle(srvIndex_);
    if (srvHandle.ptr == 0) {
        return false;
    }

    if (!CanReleaseGpuResources(dxCommon_, resource_ != nullptr || rtvHeap_ ||
                                               srvIndex_ != UINT_MAX)) {
        return false;
    }
    device->CreateShaderResourceView(newResource.Get(), &srvDesc, srvHandle);

    resource_ = std::move(newResource);
    rtvHeap_ = std::move(newRtvHeap);
    rtvDescriptorSize_ = newRtvDescriptorSize;
    resourceState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    return true;
}

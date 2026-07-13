#include "graphics/RenderTexture.h"

#include "core/Numeric.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"
#include "internal/RenderTextureInternal.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;
using Numeric::ClampFinite;

class RenderTextureInitializationGuard {
public:
    explicit RenderTextureInitializationGuard(RenderTexture& target) : target_(target) {}
    ~RenderTextureInitializationGuard() {
        if (active_) {
            target_.Release();
        }
    }

    RenderTextureInitializationGuard(const RenderTextureInitializationGuard&) = delete;
    RenderTextureInitializationGuard& operator=(const RenderTextureInitializationGuard&) = delete;

    void Commit() {
        active_ = false;
    }

private:
    RenderTexture& target_;
    bool active_ = true;
};
} // namespace

RenderTexture::RenderTexture() : resources_(std::make_unique<State>()) {}

RenderTexture::~RenderTexture() {
    Release(true);
}

int RenderTexture::GetWidth() const {
    return resources_->width;
}

int RenderTexture::GetHeight() const {
    return resources_->height;
}

bool RenderTexture::IsReady() const {
    return dxCommon_ != nullptr && srvManager_ != nullptr && resources_->resource &&
           resources_->rtvHeap && resources_->srvIndex != UINT_MAX && GetGpuHandle().ptr != 0;
}

void RenderTexture::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, int width,
                               int height) {
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
    resources_->srvIndex = srvManager_->Allocate();
    if (resources_->srvIndex == UINT_MAX) {
        return;
    }
    resources_->width = width;
    resources_->height = height;

    if (!CreateResources() || !resources_->resource || !resources_->rtvHeap ||
        GetGpuHandle().ptr == 0) {
        return;
    }
    initializeGuard.Commit();
}

bool RenderTexture::Resize(int width, int height) {
    if (width <= 0 || height <= 0 ||
        (width == resources_->width && height == resources_->height && resources_->resource &&
         resources_->rtvHeap)) {
        return width > 0 && height > 0 && IsReady();
    }
    if (!dxCommon_ || !srvManager_ || resources_->srvIndex == UINT_MAX) {
        return false;
    }
    if (dxCommon_->IsCommandListRecording()) {
        return false;
    }

    const int previousWidth = resources_->width;
    const int previousHeight = resources_->height;
    resources_->width = width;
    resources_->height = height;

    if (!CreateResources()) {
        resources_->width = previousWidth;
        resources_->height = previousHeight;
        return false;
    }
    return IsReady();
}

bool RenderTexture::Release() {
    return Release(false);
}

bool RenderTexture::Release(bool allowFrameAbort) {
    if (!ReleaseTextureResources(allowFrameAbort)) {
        return false;
    }
    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }

    if (srvManager_ != nullptr && resources_->srvIndex != UINT_MAX) {
        srvManager_->FreeIfAllocated(resources_->srvIndex);
        resources_->srvIndex = UINT_MAX;
    }

    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    resources_->width = 0;
    resources_->height = 0;
    return true;
}

void RenderTexture::BeginRender(const DirectX::XMFLOAT4& clearColor) {
    BeginRenderInternal(clearColor, true, true);
}

void RenderTexture::BeginRenderNoDepth(const DirectX::XMFLOAT4& clearColor) {
    BeginRenderInternal(clearColor, false, false);
}

void RenderTexture::BeginRenderInternal(const DirectX::XMFLOAT4& clearColor, bool bindDepth,
                                        bool clearDepth) {
    if (!dxCommon_ || !resources_->resource || !resources_->rtvHeap) {
        return;
    }

    auto* commandList = dxCommon_->GetCommandList();
    if (commandList == nullptr) {
        return;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandlePtr = nullptr;
    if (bindDepth) {
        dsvHandle = dxCommon_->GetDepthStencilView();
        if (dsvHandle.ptr == 0) {
            return;
        }
        dsvHandlePtr = &dsvHandle;
    }

    if (resources_->resourceState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        const D3D12_RESOURCE_STATES previousState = resources_->resourceState;
        if (!dxCommon_->RegisterFrameRollback(
                this, [this, previousState]() { resources_->resourceState = previousState; })) {
            return;
        }
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resources_->resource.Get(),
                                                            resources_->resourceState,
                                                            D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandList->ResourceBarrier(1, &barrier);
        resources_->resourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    D3D12_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(resources_->width);
    viewport.Height = static_cast<float>(resources_->height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect{};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = resources_->width;
    scissorRect.bottom = resources_->height;

    auto rtvHandle = resources_->rtvHeap->GetCPUDescriptorHandleForHeapStart();

    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, dsvHandlePtr);

    const float clear[4] = {
        ClampFinite(clearColor.x, 0.0f, 1.0f, 0.0f),
        ClampFinite(clearColor.y, 0.0f, 1.0f, 0.0f),
        ClampFinite(clearColor.z, 0.0f, 1.0f, 0.0f),
        ClampFinite(clearColor.w, 0.0f, 1.0f, 1.0f),
    };
    commandList->ClearRenderTargetView(rtvHandle, clear, 0, nullptr);
    if (clearDepth && dsvHandle.ptr != 0) {
        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    }
}

void RenderTexture::EndRender() {
    if (!dxCommon_ || !resources_->resource) {
        return;
    }

    if (resources_->resourceState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        auto* commandList = dxCommon_->GetCommandList();
        if (commandList == nullptr) {
            return;
        }
        const D3D12_RESOURCE_STATES previousState = resources_->resourceState;
        if (!dxCommon_->RegisterFrameRollback(
                this, [this, previousState]() { resources_->resourceState = previousState; })) {
            return;
        }
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            resources_->resource.Get(), resources_->resourceState,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commandList->ResourceBarrier(1, &barrier);
        resources_->resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
}

D3D12_GPU_DESCRIPTOR_HANDLE RenderTexture::GetGpuHandle() const {
    if (!resources_->resource || !srvManager_ || resources_->srvIndex == UINT_MAX) {
        return {};
    }

    return srvManager_->GetGpuHandle(resources_->srvIndex);
}

bool RenderTexture::ReleaseTextureResources() {
    return ReleaseTextureResources(false);
}

bool RenderTexture::ReleaseTextureResources(bool allowFrameAbort) {
    if (!CanReleaseGpuResources(dxCommon_,
                                resources_->resource != nullptr || resources_->rtvHeap ||
                                    resources_->srvIndex != UINT_MAX,
                                allowFrameAbort)) {
        return false;
    }

    resources_->resource.Reset();
    resources_->rtvHeap.Reset();
    resources_->rtvDescriptorSize = 0;
    resources_->resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    return true;
}

bool RenderTexture::CreateResources() {
    auto* device = dxCommon_->GetDevice();
    if (device == nullptr || srvManager_ == nullptr || resources_->srvIndex == UINT_MAX) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> newRtvHeap;
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&newRtvHeap))) ||
        !newRtvHeap) {
        return false;
    }

    const UINT newRtvDescriptorSize =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    auto resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DirectXCommon::kSceneColorFormat, static_cast<UINT64>(resources_->width),
        static_cast<UINT>(resources_->height), 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DirectXCommon::kSceneColorFormat;
    clearValue.Color[0] = 0.1f;
    clearValue.Color[1] = 0.2f;
    clearValue.Color[2] = 0.4f;
    clearValue.Color[3] = 1.0f;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    Microsoft::WRL::ComPtr<ID3D12Resource> newResource;
    if (!CreateCommittedResourceChecked(device, &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
                                        newResource.GetAddressOf())) {
        return false;
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DirectXCommon::kSceneColorFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(newResource.Get(), &rtvDesc,
                                   newRtvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DirectXCommon::kSceneColorFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    const D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = srvManager_->GetCpuHandle(resources_->srvIndex);
    if (srvHandle.ptr == 0) {
        return false;
    }

    if (!CanReleaseGpuResources(dxCommon_, resources_->resource != nullptr ||
                                               resources_->rtvHeap != nullptr)) {
        return false;
    }
    device->CreateShaderResourceView(newResource.Get(), &srvDesc, srvHandle);

    resources_->resource = std::move(newResource);
    resources_->rtvHeap = std::move(newRtvHeap);
    resources_->rtvDescriptorSize = newRtvDescriptorSize;
    resources_->resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    return true;
}

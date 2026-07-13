#include "graphics/ShadowMapRenderer.h"

#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"
#include "internal/ShadowMapRendererInternal.h"

#include <algorithm>
#include <limits>

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;

class ShadowMapInitializationGuard {
public:
    explicit ShadowMapInitializationGuard(ShadowMapRenderer& target) : target_(target) {}
    ~ShadowMapInitializationGuard() {
        if (active_) {
            target_.Release();
        }
    }

    ShadowMapInitializationGuard(const ShadowMapInitializationGuard&) = delete;
    ShadowMapInitializationGuard& operator=(const ShadowMapInitializationGuard&) = delete;

    void Commit() {
        active_ = false;
    }

private:
    ShadowMapRenderer& target_;
    bool active_ = true;
};
} // namespace

ShadowMapRenderer::ShadowMapRenderer() : resources_(std::make_unique<State>()) {}

ShadowMapRenderer::~ShadowMapRenderer() {
    Release(true);
}

const DirectX::XMFLOAT4X4& ShadowMapRenderer::GetLightViewProjection() const {
    return resources_->lightViewProjection;
}

void ShadowMapRenderer::SetLightViewProjection(const DirectX::XMFLOAT4X4& matrix) {
    resources_->lightViewProjection = matrix;
}

uint32_t ShadowMapRenderer::GetWidth() const {
    return resources_->width;
}

uint32_t ShadowMapRenderer::GetHeight() const {
    return resources_->height;
}

bool ShadowMapRenderer::IsReady() const {
    return dxCommon_ != nullptr && srvManager_ != nullptr && resources_->depthTexture &&
           resources_->dsvHeap && IsValidResourceId(resources_->srvIndex) &&
           resources_->srvGpuHandle.ptr != 0;
}

void ShadowMapRenderer::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width,
                                   uint32_t height) {
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager) {
        Release();
        return;
    }

    if (!Release()) {
        return;
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    ShadowMapInitializationGuard initializeGuard(*this);
    if (!srvManager_->CanAllocate()) {
        return;
    }
    resources_->srvIndex = srvManager_->Allocate();
    if (!IsValidResourceId(resources_->srvIndex)) {
        return;
    }
    if (!Resize(width, height)) {
        return;
    }
    initializeGuard.Commit();
}

bool ShadowMapRenderer::Release() {
    return Release(false);
}

bool ShadowMapRenderer::Release(bool allowFrameAbort) {
    if (!ReleaseDepthResources(allowFrameAbort)) {
        return false;
    }
    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }

    if (srvManager_ != nullptr && IsValidResourceId(resources_->srvIndex)) {
        srvManager_->FreeIfAllocated(resources_->srvIndex);
    }

    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    resources_->srvIndex = kInvalidResourceId;
    resources_->srvGpuHandle = {};
    resources_->resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    return true;
}

bool ShadowMapRenderer::Resize(uint32_t width, uint32_t height) {
    if (!dxCommon_ || !srvManager_ || !IsValidResourceId(resources_->srvIndex)) {
        return false;
    }

    constexpr uint32_t kMaxShadowMapSize =
        static_cast<uint32_t>((std::numeric_limits<LONG>::max)());
    const uint32_t newWidth = std::clamp(width, 1u, kMaxShadowMapSize);
    const uint32_t newHeight = std::clamp(height, 1u, kMaxShadowMapSize);
    if (newWidth == resources_->width && newHeight == resources_->height &&
        resources_->depthTexture && resources_->dsvHeap && resources_->srvGpuHandle.ptr != 0) {
        return true;
    }
    if (dxCommon_->IsCommandListRecording()) {
        return false;
    }

    const uint32_t oldWidth = resources_->width;
    const uint32_t oldHeight = resources_->height;
    const D3D12_VIEWPORT oldViewport = resources_->viewport;
    const D3D12_RECT oldScissor = resources_->scissor;
    const D3D12_RESOURCE_STATES oldState = resources_->resourceState;
    const D3D12_GPU_DESCRIPTOR_HANDLE oldSrvGpuHandle = resources_->srvGpuHandle;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> oldDsvHeap = resources_->dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> oldDepthTexture = resources_->depthTexture;

    if (!ReleaseDepthResources()) {
        return false;
    }
    resources_->width = newWidth;
    resources_->height = newHeight;

    resources_->viewport.TopLeftX = 0.0f;
    resources_->viewport.TopLeftY = 0.0f;
    resources_->viewport.Width = static_cast<float>(resources_->width);
    resources_->viewport.Height = static_cast<float>(resources_->height);
    resources_->viewport.MinDepth = 0.0f;
    resources_->viewport.MaxDepth = 1.0f;

    resources_->scissor.left = 0;
    resources_->scissor.top = 0;
    resources_->scissor.right = static_cast<LONG>(resources_->width);
    resources_->scissor.bottom = static_cast<LONG>(resources_->height);

    if (CreateResources() && UpdateSrv()) {
        return true;
    }

    if (!ReleaseDepthResources()) {
        return false;
    }
    resources_->depthTexture = std::move(oldDepthTexture);
    resources_->dsvHeap = std::move(oldDsvHeap);
    resources_->srvGpuHandle = oldSrvGpuHandle;
    resources_->resourceState = oldState;
    resources_->width = oldWidth;
    resources_->height = oldHeight;
    resources_->viewport = oldViewport;
    resources_->scissor = oldScissor;
    return false;
}

void ShadowMapRenderer::Begin() {
    if (!dxCommon_ || !resources_->depthTexture || !resources_->dsvHeap) {
        return;
    }

    auto* commandList = dxCommon_->GetCommandList();
    if (commandList == nullptr || GetDsvHandle().ptr == 0) {
        return;
    }

    if (resources_->resourceState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        const D3D12_RESOURCE_STATES previousState = resources_->resourceState;
        if (!dxCommon_->RegisterFrameRollback(
                this, [this, previousState]() { resources_->resourceState = previousState; })) {
            return;
        }
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resources_->depthTexture.Get(),
                                                            resources_->resourceState,
                                                            D3D12_RESOURCE_STATE_DEPTH_WRITE);
        commandList->ResourceBarrier(1, &barrier);
        resources_->resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = GetDsvHandle();
    commandList->RSSetViewports(1, &resources_->viewport);
    commandList->RSSetScissorRects(1, &resources_->scissor);
    commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void ShadowMapRenderer::End() {
    if (!dxCommon_ || !resources_->depthTexture) {
        return;
    }

    auto* commandList = dxCommon_->GetCommandList();
    if (commandList == nullptr) {
        return;
    }

    if (resources_->resourceState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        const D3D12_RESOURCE_STATES previousState = resources_->resourceState;
        if (!dxCommon_->RegisterFrameRollback(
                this, [this, previousState]() { resources_->resourceState = previousState; })) {
            return;
        }
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            resources_->depthTexture.Get(), resources_->resourceState,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commandList->ResourceBarrier(1, &barrier);
        resources_->resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
}

D3D12_GPU_DESCRIPTOR_HANDLE ShadowMapRenderer::GetGpuHandle() const {
    if (!resources_->depthTexture || !IsValidResourceId(resources_->srvIndex) ||
        resources_->srvGpuHandle.ptr == 0) {
        return {};
    }

    return resources_->srvGpuHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE ShadowMapRenderer::GetDsvHandle() const {
    if (!resources_->dsvHeap) {
        return {};
    }

    return resources_->dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

bool ShadowMapRenderer::ReleaseDepthResources() {
    return ReleaseDepthResources(false);
}

bool ShadowMapRenderer::ReleaseDepthResources(bool allowFrameAbort) {
    if (!CanReleaseGpuResources(dxCommon_,
                                resources_->depthTexture != nullptr || resources_->dsvHeap ||
                                    IsValidResourceId(resources_->srvIndex),
                                allowFrameAbort)) {
        return false;
    }

    resources_->depthTexture.Reset();
    resources_->dsvHeap.Reset();
    resources_->srvGpuHandle = {};
    resources_->resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    return true;
}

bool ShadowMapRenderer::CreateResources() {
    auto* device = dxCommon_->GetDevice();
    if (device == nullptr) {
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    if (FAILED(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&resources_->dsvHeap))) ||
        !resources_->dsvHeap) {
        return false;
    }

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = resources_->width;
    desc.Height = resources_->height;
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
    if (!CreateCommittedResourceChecked(device, &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
                                        resources_->depthTexture.GetAddressOf())) {
        resources_->dsvHeap.Reset();
        return false;
    }
    resources_->resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetDsvHandle();
    if (dsvHandle.ptr == 0) {
        resources_->depthTexture.Reset();
        resources_->dsvHeap.Reset();
        return false;
    }
    device->CreateDepthStencilView(resources_->depthTexture.Get(), &dsvDesc, dsvHandle);
    return true;
}

bool ShadowMapRenderer::UpdateSrv() {
    if (!dxCommon_ || !dxCommon_->GetDevice() || !srvManager_ ||
        !IsValidResourceId(resources_->srvIndex) || !resources_->depthTexture) {
        resources_->srvGpuHandle = {};
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    const D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = srvManager_->GetCpuHandle(resources_->srvIndex);
    const D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle =
        srvManager_->GetGpuHandle(resources_->srvIndex);
    if (srvHandle.ptr == 0 || srvGpuHandle.ptr == 0) {
        resources_->srvGpuHandle = {};
        return false;
    }
    dxCommon_->GetDevice()->CreateShaderResourceView(resources_->depthTexture.Get(), &srvDesc,
                                                     srvHandle);
    resources_->srvGpuHandle = srvGpuHandle;
    return true;
}

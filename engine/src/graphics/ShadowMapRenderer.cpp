#include "graphics/ShadowMapRenderer.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include "graphics/SrvManager.h"
#include <algorithm>
#include <stdexcept>

using namespace DxUtils;

void ShadowMapRenderer::Initialize(DirectXCommon *dxCommon,
                                   SrvManager *srvManager, uint32_t width,
                                   uint32_t height) {
    if (!dxCommon || !srvManager) {
        throw std::runtime_error("ShadowMapRenderer::Initialize null argument");
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    srvIndex_ = srvManager_->Allocate();
    srvGpuHandle_ = srvManager_->GetGpuHandle(srvIndex_);
    Resize(width, height);
}

void ShadowMapRenderer::Resize(uint32_t width, uint32_t height) {
    width_ = (std::max)(width, 1u);
    height_ = (std::max)(height, 1u);

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

    CreateResources();
    UpdateSrv();
}

void ShadowMapRenderer::Begin() {
    auto commandList = dxCommon_->GetCommandList();

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
    auto commandList = dxCommon_->GetCommandList();

    if (state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            depthTexture_.Get(), state_,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commandList->ResourceBarrier(1, &barrier);
        state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE ShadowMapRenderer::GetDsvHandle() const {
    return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
}

void ShadowMapRenderer::CreateResources() {
    auto device = dxCommon_->GetDevice();

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc,
                                               IID_PPV_ARGS(&dsvHeap_)),
                  "Create shadow DSV heap failed");

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
    ThrowIfFailed(device->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
                      IID_PPV_ARGS(&depthTexture_)),
                  "Create shadow depth texture failed");
    state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(depthTexture_.Get(), &dsvDesc,
                                   GetDsvHandle());
}

void ShadowMapRenderer::UpdateSrv() {
    if (!srvManager_ || srvIndex_ == UINT32_MAX || !depthTexture_) {
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    dxCommon_->GetDevice()->CreateShaderResourceView(
        depthTexture_.Get(), &srvDesc, srvManager_->GetCpuHandle(srvIndex_));
    srvGpuHandle_ = srvManager_->GetGpuHandle(srvIndex_);
}

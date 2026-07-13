#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/SrvManager.h"
#include "internal/DirectXCommonInternal.h"
#include "internal/DirectXCommonState.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <wrl.h>

using DirectXCommonInternal::LogIfFailed;

namespace {

Microsoft::WRL::ComPtr<IDXGIAdapter1> PickHighPerformanceAdapter(IDXGIFactory7* factory) {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (!factory) {
        return adapter;
    }

    for (UINT index = 0;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
        if (FAILED(factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                       IID_PPV_ARGS(&candidate)))) {
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        candidate->GetDesc1(&desc);
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0,
                                        __uuidof(ID3D12Device), nullptr))) {
            adapter = candidate;
            break;
        }
    }

    return adapter;
}

} // namespace

bool DirectXCommon::IsDeviceRemoved() const {
    return state_->device && FAILED(state_->device->GetDeviceRemovedReason());
}
void DirectXCommon::CreateFactory() {
    if (LogIfFailed(CreateDXGIFactory(IID_PPV_ARGS(&state_->factory)),
                    "CreateDXGIFactory failed")) {
        state_->factory.Reset();
    }
}

void DirectXCommon::CreateDevice() {
#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings)))) {
        dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetWatsonDumpEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }
#endif

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter =
        PickHighPerformanceAdapter(state_->factory.Get());
    IUnknown* deviceAdapter = adapter ? adapter.Get() : nullptr;
    if (LogIfFailed(
            D3D12CreateDevice(deviceAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&state_->device)),
            "D3D12CreateDevice failed") ||
        !state_->device) {
        state_->device.Reset();
        return;
    }
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> actualAdapter;
    if (SUCCEEDED(state_->device.As(&dxgiDevice)) &&
        SUCCEEDED(dxgiDevice->GetAdapter(&actualAdapter))) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> actualAdapter1;
        if (SUCCEEDED(actualAdapter.As(&actualAdapter1))) {
            actualAdapter1->GetDesc1(&state_->adapterDesc);
        }
    }
    state_->device->SetName(L"DirectXCommon.Device");
}

void DirectXCommon::CreateCommandQueue() {
    if (!state_->device) {
        return;
    }
    D3D12_COMMAND_QUEUE_DESC desc{};
    if (LogIfFailed(state_->device->CreateCommandQueue(&desc, IID_PPV_ARGS(&state_->commandQueue)),
                    "CreateCommandQueue failed") ||
        !state_->commandQueue) {
        state_->commandQueue.Reset();
        return;
    }
    state_->commandQueue->SetName(L"DirectXCommon.CommandQueue");
}

void DirectXCommon::CreateCommandAllocator() {
    if (!state_->device) {
        return;
    }
    for (UINT i = 0; i < kSwapChainBufferCount; ++i) {
        if (LogIfFailed(
                state_->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       IID_PPV_ARGS(&state_->commandAllocators[i])),
                "CreateCommandAllocator failed") ||
            !state_->commandAllocators[i]) {
            state_->commandAllocators[i].Reset();
            return;
        }
        wchar_t name[64]{};
        swprintf_s(name, L"DirectXCommon.CommandAllocator[%u]", i);
        state_->commandAllocators[i]->SetName(name);
    }
}

void DirectXCommon::CreateCommandList() {
    if (!state_->device || !state_->commandAllocators[state_->backBufferIndex]) {
        return;
    }
    if (LogIfFailed(state_->device->CreateCommandList(
                        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                        state_->commandAllocators[state_->backBufferIndex].Get(), nullptr,
                        IID_PPV_ARGS(&state_->commandList)),
                    "CreateCommandList failed") ||
        !state_->commandList) {
        state_->commandList.Reset();
        return;
    }
    state_->commandList->SetName(L"DirectXCommon.CommandList");

    if (LogIfFailed(state_->commandList->Close(), "state_->commandList->Close failed")) {
        state_->commandList.Reset();
    }
}

void DirectXCommon::CreateSwapChain(HWND hwnd, int width, int height) {
    if (!state_->factory || !state_->commandQueue || hwnd == nullptr || width <= 0 || height <= 0) {
        return;
    }
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = kBackBufferFormat;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kSwapChainBufferCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> sc1;
    if (LogIfFailed(state_->factory->CreateSwapChainForHwnd(state_->commandQueue.Get(), hwnd, &desc,
                                                            nullptr, nullptr, &sc1),
                    "CreateSwapChainForHwnd failed") ||
        !sc1) {
        state_->swapChain.Reset();
        return;
    }

    if (LogIfFailed(sc1.As(&state_->swapChain), "SwapChain As() failed") || !state_->swapChain) {
        state_->swapChain.Reset();
        return;
    }

    state_->backBufferIndex = state_->swapChain->GetCurrentBackBufferIndex();
}

void DirectXCommon::CreateRTV() {
    if (!state_->device || !state_->swapChain) {
        return;
    }

    auto resetRtvState = [this]() {
        std::for_each(std::begin(state_->backBuffers), std::end(state_->backBuffers),
                      [](auto& backBuffer) { backBuffer.Reset(); });
        std::fill(std::begin(state_->backBufferStates), std::end(state_->backBufferStates),
                  D3D12_RESOURCE_STATE_PRESENT);
        state_->rtvHeap.Reset();
        state_->rtvDescriptorSize = 0;
    };

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = kSwapChainBufferCount + 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (LogIfFailed(state_->device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&state_->rtvHeap)),
                    "CreateDescriptorHeap(RTV) failed") ||
        !state_->rtvHeap) {
        resetRtvState();
        return;
    }
    state_->rtvHeap->SetName(L"DirectXCommon.RtvHeap");

    state_->rtvDescriptorSize =
        state_->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(state_->rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < kSwapChainBufferCount; i++) {
        if (LogIfFailed(state_->swapChain->GetBuffer(i, IID_PPV_ARGS(&state_->backBuffers[i])),
                        "state_->swapChain->GetBuffer failed") ||
            !state_->backBuffers[i]) {
            resetRtvState();
            return;
        }
        wchar_t name[64]{};
        swprintf_s(name, L"DirectXCommon.BackBuffer[%u]", i);
        state_->backBuffers[i]->SetName(name);

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Format = kBackBufferFormat;

        state_->device->CreateRenderTargetView(state_->backBuffers[i].Get(), &rtvDesc, handle);
        state_->backBufferStates[i] = D3D12_RESOURCE_STATE_PRESENT;

        handle.Offset(1, state_->rtvDescriptorSize);
    }
}

void DirectXCommon::CreateSceneRenderTarget(int width, int height) {
    auto resetSceneColorState = [this]() {
        state_->sceneColorBuffer.Reset();
        state_->sceneColorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    };

    if (!state_->device || !state_->rtvHeap || width <= 0 || height <= 0) {
        resetSceneColorState();
        return;
    }
    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = static_cast<UINT64>(width);
    resDesc.Height = static_cast<UINT>(height);
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = kSceneColorFormat;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = kSceneColorFormat;
    clearValue.Color[0] = state_->clearColor[0];
    clearValue.Color[1] = state_->clearColor[1];
    clearValue.Color[2] = state_->clearColor[2];
    clearValue.Color[3] = state_->clearColor[3];

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    if (LogIfFailed(state_->device->CreateCommittedResource(
                        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
                        IID_PPV_ARGS(&state_->sceneColorBuffer)),
                    "CreateCommittedResource(SceneRenderTarget) failed") ||
        !state_->sceneColorBuffer) {
        resetSceneColorState();
        return;
    }
    state_->sceneColorBuffer->SetName(L"DirectXCommon.SceneColorBuffer");
    state_->sceneColorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Format = kSceneColorFormat;

    const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = GetSceneRtvHandle();
    if (sceneRtv.ptr == 0) {
        resetSceneColorState();
        return;
    }
    state_->device->CreateRenderTargetView(state_->sceneColorBuffer.Get(), &rtvDesc, sceneRtv);
}

void DirectXCommon::CreateViewport(int width, int height) {
    state_->sceneViewport.TopLeftX = 0.0f;
    state_->sceneViewport.TopLeftY = 0.0f;
    state_->sceneViewport.Width = static_cast<float>(width);
    state_->sceneViewport.Height = static_cast<float>(height);
    state_->sceneViewport.MinDepth = 0.0f;
    state_->sceneViewport.MaxDepth = 1.0f;
}

void DirectXCommon::CreateScissor(int width, int height) {
    state_->sceneScissorRect.left = 0;
    state_->sceneScissorRect.top = 0;
    state_->sceneScissorRect.right = width;
    state_->sceneScissorRect.bottom = height;
}

void DirectXCommon::CreateDepthStencil(int width, int height) {
    auto resetDepthStencilState = [this]() {
        state_->dsvHeap.Reset();
        state_->depthBuffer.Reset();
        state_->depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    };

    if (!state_->device || width <= 0 || height <= 0) {
        resetDepthStencilState();
        return;
    }
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 1;

    if (LogIfFailed(state_->device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&state_->dsvHeap)),
                    "CreateDescriptorHeap(DSV) failed") ||
        !state_->dsvHeap) {
        resetDepthStencilState();
        return;
    }
    state_->dsvHeap->SetName(L"DirectXCommon.DsvHeap");

    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = width;
    resDesc.Height = height;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = kDepthResourceFormat;
    resDesc.SampleDesc.Count = 1;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = kDepthStencilFormat;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    if (LogIfFailed(
            state_->device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                                                    D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
                                                    IID_PPV_ARGS(&state_->depthBuffer)),
            "CreateCommittedResource(DepthStencil) failed") ||
        !state_->depthBuffer) {
        resetDepthStencilState();
        return;
    }
    state_->depthBuffer->SetName(L"DirectXCommon.DepthBuffer");

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDescView{};
    dsvDescView.Format = kDepthStencilFormat;
    dsvDescView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    state_->device->CreateDepthStencilView(state_->depthBuffer.Get(), &dsvDescView,
                                           state_->dsvHeap->GetCPUDescriptorHandleForHeapStart());
    state_->depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}

bool DirectXCommon::UpdateDepthStencilSrv() {
    if (!state_->device || !state_->srvManager || state_->depthSrvIndex == UINT_MAX ||
        !state_->depthBuffer) {
        state_->depthSrvGpuHandle = {};
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = kDepthSrvFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    const D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
        state_->srvManager->GetCpuHandle(state_->depthSrvIndex);
    const D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle =
        state_->srvManager->GetGpuHandle(state_->depthSrvIndex);
    if (srvHandle.ptr == 0 || srvGpuHandle.ptr == 0) {
        state_->depthSrvGpuHandle = {};
        return false;
    }
    state_->device->CreateShaderResourceView(state_->depthBuffer.Get(), &srvDesc, srvHandle);
    state_->depthSrvGpuHandle = srvGpuHandle;
    return true;
}

bool DirectXCommon::UpdateSceneColorSrv() {
    if (!state_->device || !state_->srvManager || state_->sceneSrvIndex == UINT_MAX ||
        !state_->sceneColorBuffer) {
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = kSceneColorFormat;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    const D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
        state_->srvManager->GetCpuHandle(state_->sceneSrvIndex);
    if (srvHandle.ptr == 0) {
        return false;
    }
    state_->device->CreateShaderResourceView(state_->sceneColorBuffer.Get(), &srvDesc, srvHandle);
    return state_->srvManager->GetGpuHandle(state_->sceneSrvIndex).ptr != 0;
}
D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetBackBufferRtvHandle() const {
    if (!state_->rtvHeap || state_->rtvDescriptorSize == 0 ||
        state_->backBufferIndex >= kSwapChainBufferCount) {
        return {};
    }
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(state_->rtvHeap->GetCPUDescriptorHandleForHeapStart(),
                                         static_cast<INT>(state_->backBufferIndex),
                                         static_cast<INT>(state_->rtvDescriptorSize));
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetSceneRtvHandle() const {
    if (!state_->rtvHeap || state_->rtvDescriptorSize == 0) {
        return {};
    }

    return CD3DX12_CPU_DESCRIPTOR_HANDLE(state_->rtvHeap->GetCPUDescriptorHandleForHeapStart(),
                                         static_cast<INT>(kSceneRtvIndex),
                                         static_cast<INT>(state_->rtvDescriptorSize));
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetDepthStencilView() const {
    if (!state_->dsvHeap || !state_->depthBuffer) {
        return {};
    }

    return state_->dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE
DirectXCommon::GetDepthStencilGpuHandle() const {
    if (state_->depthSrvIndex == UINT_MAX || state_->depthSrvGpuHandle.ptr == 0 ||
        !state_->depthBuffer) {
        return {};
    }

    return state_->depthSrvGpuHandle;
}

D3D12_GPU_DESCRIPTOR_HANDLE
DirectXCommon::GetSceneSrvGpuHandle(const SrvManager* srvManager) const {
    if (srvManager == nullptr) {
        return {};
    }
    if (state_->sceneSrvIndex == UINT_MAX || !state_->sceneColorBuffer) {
        return {};
    }

    return srvManager->GetGpuHandle(state_->sceneSrvIndex);
}

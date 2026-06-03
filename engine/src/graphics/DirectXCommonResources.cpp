#include "graphics/DirectXCommon.h"
#include "DirectXCommonInternal.h"

#include "graphics/DxHelpers.h"
#include "graphics/SrvManager.h"

#include <cstdio>
#include <wrl.h>

using DirectXCommonInternal::LogIfFailed;

namespace {

Microsoft::WRL::ComPtr<IDXGIAdapter1>
PickHighPerformanceAdapter(IDXGIFactory7 *factory) {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (!factory) {
        return adapter;
    }

    for (UINT index = 0;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
        if (FAILED(factory->EnumAdapterByGpuPreference(
                index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&candidate)))) {
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        candidate->GetDesc1(&desc);
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(candidate.Get(),
                                        D3D_FEATURE_LEVEL_11_0,
                                        __uuidof(ID3D12Device), nullptr))) {
            adapter = candidate;
            break;
        }
    }

    return adapter;
}

} // namespace

bool DirectXCommon::IsDeviceRemoved() const {
    return device_ && FAILED(device_->GetDeviceRemovedReason());
}
void DirectXCommon::CreateFactory() {
    if (LogIfFailed(CreateDXGIFactory(IID_PPV_ARGS(&factory_)),
                    "CreateDXGIFactory failed")) {
        factory_.Reset();
    }
}

void DirectXCommon::CreateDevice() {
#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings>
        dredSettings;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings)))) {
        dredSettings->SetAutoBreadcrumbsEnablement(
            D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetWatsonDumpEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }
#endif

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter =
        PickHighPerformanceAdapter(factory_.Get());
    IUnknown *deviceAdapter = adapter ? adapter.Get() : nullptr;
    if (LogIfFailed(D3D12CreateDevice(deviceAdapter, D3D_FEATURE_LEVEL_11_0,
                                      IID_PPV_ARGS(&device_)),
                    "D3D12CreateDevice failed") ||
        !device_) {
        device_.Reset();
        return;
    }
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> actualAdapter;
    if (SUCCEEDED(device_.As(&dxgiDevice)) &&
        SUCCEEDED(dxgiDevice->GetAdapter(&actualAdapter))) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> actualAdapter1;
        if (SUCCEEDED(actualAdapter.As(&actualAdapter1))) {
            actualAdapter1->GetDesc1(&adapterDesc_);
        }
    }
    device_->SetName(L"DirectXCommon.Device");
}

void DirectXCommon::CreateCommandQueue() {
    if (!device_) {
        return;
    }
    D3D12_COMMAND_QUEUE_DESC desc{};
    if (LogIfFailed(
            device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue_)),
            "CreateCommandQueue failed") ||
        !commandQueue_) {
        commandQueue_.Reset();
        return;
    }
    commandQueue_->SetName(L"DirectXCommon.CommandQueue");
}

void DirectXCommon::CreateCommandAllocator() {
    if (!device_) {
        return;
    }
    for (UINT i = 0; i < kSwapChainBufferCount; ++i) {
        if (LogIfFailed(
                device_->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(&commandAllocators_[i])),
                "CreateCommandAllocator failed") ||
            !commandAllocators_[i]) {
            commandAllocators_[i].Reset();
            return;
        }
        wchar_t name[64]{};
        swprintf_s(name, L"DirectXCommon.CommandAllocator[%u]", i);
        commandAllocators_[i]->SetName(name);
    }
}

void DirectXCommon::CreateCommandList() {
    if (!device_ || !commandAllocators_[backBufferIndex_]) {
        return;
    }
    if (LogIfFailed(device_->CreateCommandList(
                        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                        commandAllocators_[backBufferIndex_].Get(), nullptr,
                        IID_PPV_ARGS(&commandList_)),
                    "CreateCommandList failed") ||
        !commandList_) {
        commandList_.Reset();
        return;
    }
    commandList_->SetName(L"DirectXCommon.CommandList");

    if (LogIfFailed(commandList_->Close(), "commandList_->Close failed")) {
        commandList_.Reset();
    }
}

void DirectXCommon::CreateSwapChain(HWND hwnd, int width, int height) {
    if (!factory_ || !commandQueue_ || hwnd == nullptr || width <= 0 ||
        height <= 0) {
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
    if (LogIfFailed(factory_->CreateSwapChainForHwnd(
                        commandQueue_.Get(), hwnd, &desc, nullptr, nullptr,
                        &sc1),
                    "CreateSwapChainForHwnd failed") ||
        !sc1) {
        swapChain_.Reset();
        return;
    }

    if (LogIfFailed(sc1.As(&swapChain_), "SwapChain As() failed") ||
        !swapChain_) {
        swapChain_.Reset();
        return;
    }

    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
}

void DirectXCommon::CreateRTV() {
    if (!device_ || !swapChain_) {
        return;
    }

    auto resetRtvState = [this]() {
        for (auto &backBuffer : backBuffers_) {
            backBuffer.Reset();
        }
        for (auto &state : backBufferStates_) {
            state = D3D12_RESOURCE_STATE_PRESENT;
        }
        rtvHeap_.Reset();
        rtvDescriptorSize_ = 0;
    };

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = kSwapChainBufferCount + 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (LogIfFailed(
            device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap_)),
            "CreateDescriptorHeap(RTV) failed") ||
        !rtvHeap_) {
        resetRtvState();
        return;
    }
    rtvHeap_->SetName(L"DirectXCommon.RtvHeap");

    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
        rtvHeap_->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < kSwapChainBufferCount; i++) {
        if (LogIfFailed(
                swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i])),
                "swapChain_->GetBuffer failed") ||
            !backBuffers_[i]) {
            resetRtvState();
            return;
        }
        wchar_t name[64]{};
        swprintf_s(name, L"DirectXCommon.BackBuffer[%u]", i);
        backBuffers_[i]->SetName(name);

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Format = kBackBufferFormat;

        device_->CreateRenderTargetView(backBuffers_[i].Get(), &rtvDesc,
                                        handle);
        backBufferStates_[i] = D3D12_RESOURCE_STATE_PRESENT;

        handle.Offset(1, rtvDescriptorSize_);
    }
}

void DirectXCommon::CreateSceneRenderTarget(int width, int height) {
    auto resetSceneColorState = [this]() {
        sceneColorBuffer_.Reset();
        sceneColorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    };

    if (!device_ || !rtvHeap_ || width <= 0 || height <= 0) {
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
    clearValue.Color[0] = clearColor_[0];
    clearValue.Color[1] = clearColor_[1];
    clearValue.Color[2] = clearColor_[2];
    clearValue.Color[3] = clearColor_[3];

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    if (LogIfFailed(device_->CreateCommittedResource(
                        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
                        IID_PPV_ARGS(&sceneColorBuffer_)),
                    "CreateCommittedResource(SceneRenderTarget) failed") ||
        !sceneColorBuffer_) {
        resetSceneColorState();
        return;
    }
    sceneColorBuffer_->SetName(L"DirectXCommon.SceneColorBuffer");
    sceneColorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Format = kSceneColorFormat;

    const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = GetSceneRtvHandle();
    if (sceneRtv.ptr == 0) {
        resetSceneColorState();
        return;
    }
    device_->CreateRenderTargetView(sceneColorBuffer_.Get(), &rtvDesc,
                                    sceneRtv);
}

void DirectXCommon::CreateViewport(int width, int height) {
    sceneViewport_.TopLeftX = 0.0f;
    sceneViewport_.TopLeftY = 0.0f;
    sceneViewport_.Width = static_cast<float>(width);
    sceneViewport_.Height = static_cast<float>(height);
    sceneViewport_.MinDepth = 0.0f;
    sceneViewport_.MaxDepth = 1.0f;
}

void DirectXCommon::CreateScissor(int width, int height) {
    sceneScissorRect_.left = 0;
    sceneScissorRect_.top = 0;
    sceneScissorRect_.right = width;
    sceneScissorRect_.bottom = height;
}

void DirectXCommon::CreateDepthStencil(int width, int height) {
    auto resetDepthStencilState = [this]() {
        dsvHeap_.Reset();
        depthBuffer_.Reset();
        depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    };

    if (!device_ || width <= 0 || height <= 0) {
        resetDepthStencilState();
        return;
    }
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 1;

    if (LogIfFailed(
            device_->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap_)),
            "CreateDescriptorHeap(DSV) failed") ||
        !dsvHeap_) {
        resetDepthStencilState();
        return;
    }
    dsvHeap_->SetName(L"DirectXCommon.DsvHeap");

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

    if (LogIfFailed(device_->CreateCommittedResource(
                        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
                        IID_PPV_ARGS(&depthBuffer_)),
                    "CreateCommittedResource(DepthStencil) failed") ||
        !depthBuffer_) {
        resetDepthStencilState();
        return;
    }
    depthBuffer_->SetName(L"DirectXCommon.DepthBuffer");

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDescView{};
    dsvDescView.Format = kDepthStencilFormat;
    dsvDescView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    device_->CreateDepthStencilView(
        depthBuffer_.Get(), &dsvDescView,
        dsvHeap_->GetCPUDescriptorHandleForHeapStart());
    depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}

void DirectXCommon::UpdateDepthStencilSrv() {
    if (!device_ || !srvManager_ || depthSrvIndex_ == UINT_MAX ||
        !depthBuffer_) {
        depthSrvGpuHandle_ = {};
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = kDepthSrvFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    const D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
        srvManager_->GetCpuHandle(depthSrvIndex_);
    const D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle =
        srvManager_->GetGpuHandle(depthSrvIndex_);
    if (srvHandle.ptr == 0 || srvGpuHandle.ptr == 0) {
        depthSrvGpuHandle_ = {};
        return;
    }
    device_->CreateShaderResourceView(depthBuffer_.Get(), &srvDesc, srvHandle);
    depthSrvGpuHandle_ = srvGpuHandle;
}

void DirectXCommon::UpdateSceneColorSrv() {
    if (!device_ || !srvManager_ || sceneSrvIndex_ == UINT_MAX ||
        !sceneColorBuffer_) {
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = kSceneColorFormat;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    const D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
        srvManager_->GetCpuHandle(sceneSrvIndex_);
    if (srvHandle.ptr == 0) {
        return;
    }
    device_->CreateShaderResourceView(sceneColorBuffer_.Get(), &srvDesc,
                                      srvHandle);
}
D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetBackBufferRtvHandle() const {
    if (!rtvHeap_ || rtvDescriptorSize_ == 0 ||
        backBufferIndex_ >= kSwapChainBufferCount) {
        return {};
    }
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        rtvHeap_->GetCPUDescriptorHandleForHeapStart(),
        static_cast<INT>(backBufferIndex_),
        static_cast<INT>(rtvDescriptorSize_));
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetSceneRtvHandle() const {
    if (!rtvHeap_ || rtvDescriptorSize_ == 0) {
        return {};
    }

    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        rtvHeap_->GetCPUDescriptorHandleForHeapStart(),
        static_cast<INT>(kSceneRtvIndex), static_cast<INT>(rtvDescriptorSize_));
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetDepthStencilView() const {
    if (!dsvHeap_ || !depthBuffer_) {
        return {};
    }

    return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE
DirectXCommon::GetDepthStencilGpuHandle() const {
    if (depthSrvIndex_ == UINT_MAX || depthSrvGpuHandle_.ptr == 0 ||
        !depthBuffer_) {
        return {};
    }

    return depthSrvGpuHandle_;
}

D3D12_GPU_DESCRIPTOR_HANDLE
DirectXCommon::GetSceneSrvGpuHandle(const SrvManager *srvManager) const {
    if (srvManager == nullptr) {
        return {};
    }
    if (sceneSrvIndex_ == UINT_MAX || !sceneColorBuffer_) {
        return {};
    }

    return srvManager->GetGpuHandle(sceneSrvIndex_);
}
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include "graphics/SrvManager.h"
#include <stdexcept>

using namespace DxUtils;

DirectXCommon::~DirectXCommon() {
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

void DirectXCommon::Initialize(HWND hwnd, int width, int height) {
    CreateFactory();
    CreateDevice();
    CreateCommandQueue();
    CreateCommandAllocator();
    CreateCommandList();
    CreateSwapChain(hwnd, width, height);
    CreateRTV();
    CreateSceneRenderTarget(width, height);
    CreateViewport(width, height);
    CreateScissor(width, height);
    CreateDepthStencil(width, height);
    CreateFence();
}

void DirectXCommon::BeginFrame() {
    WaitForFrame(backBufferIndex_);
    ID3D12CommandAllocator* commandAllocator =
        commandAllocators_[backBufferIndex_].Get();
    ThrowIfFailed(commandAllocator->Reset(),
                  "commandAllocator_->Reset failed");
    ThrowIfFailed(commandList_->Reset(commandAllocator, nullptr),
                  "commandList_->Reset failed");
    isCommandListRecording_ = true;

    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissorRect_);
}

void DirectXCommon::BeginScenePass() {
    auto toRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(
        sceneColorBuffer_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList_->ResourceBarrier(1, &toRenderTarget);

    auto sceneRtv = GetSceneRtvHandle();
    auto dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

    commandList_->OMSetRenderTargets(1, &sceneRtv, FALSE, &dsvHandle);
    commandList_->ClearRenderTargetView(sceneRtv, kClearColor, 0, nullptr);
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f,
                                        0, 0, nullptr);
}

void DirectXCommon::EndScenePass() {
    auto toShaderResource = CD3DX12_RESOURCE_BARRIER::Transition(
        sceneColorBuffer_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList_->ResourceBarrier(1, &toShaderResource);
}

void DirectXCommon::BeginBackBufferPass(bool bindDepth) {
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffers_[backBufferIndex_].Get(), D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList_->ResourceBarrier(1, &barrier);

    SetBackBufferRenderTarget(true, bindDepth);
}

void DirectXCommon::EndFrame() {
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffers_[backBufferIndex_].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    commandList_->ResourceBarrier(1, &barrier);

    ThrowIfFailed(commandList_->Close(), "commandList_->Close failed");
    isCommandListRecording_ = false;

    ID3D12CommandList *lists[] = {commandList_.Get()};
    commandQueue_->ExecuteCommandLists(1, lists);

    HRESULT presentResult = swapChain_->Present(1, 0);
    if (FAILED(presentResult)) {
        HRESULT removedReason = device_->GetDeviceRemovedReason();
        if (FAILED(removedReason)) {
            ThrowIfFailed(removedReason, "D3D12 device removed");
        }
        ThrowIfFailed(presentResult, "swapChain_->Present failed");
    }

    const UINT presentedBufferIndex = backBufferIndex_;
    fenceValue_++;
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(), fenceValue_),
                  "commandQueue_->Signal failed");
    frameFenceValues_[presentedBufferIndex] = fenceValue_;

    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
}

void DirectXCommon::Resize(int width, int height) {
    if (!swapChain_ || width <= 0 || height <= 0) {
        return;
    }

    WaitForGpu();

    for (auto &backBuffer : backBuffers_) {
        backBuffer.Reset();
    }
    sceneColorBuffer_.Reset();
    depthBuffer_.Reset();

    ThrowIfFailed(swapChain_->ResizeBuffers(
                      kSwapChainBufferCount, static_cast<UINT>(width),
                      static_cast<UINT>(height), kBackBufferFormat, 0),
                  "swapChain_->ResizeBuffers failed");

    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

    CreateRTV();
    CreateSceneRenderTarget(width, height);
    CreateViewport(width, height);
    CreateScissor(width, height);
    CreateDepthStencil(width, height);
    UpdateDepthStencilSrv();
    UpdateSceneColorSrv();
}

void DirectXCommon::BeginUpload() {
    WaitForFrame(backBufferIndex_);
    ID3D12CommandAllocator* commandAllocator =
        commandAllocators_[backBufferIndex_].Get();
    ThrowIfFailed(commandAllocator->Reset(),
                  "commandAllocator_->Reset failed");

    ThrowIfFailed(commandList_->Reset(commandAllocator, nullptr),
                  "commandList_->Reset failed");
    isCommandListRecording_ = true;
}

void DirectXCommon::EndUpload() {
    ThrowIfFailed(commandList_->Close(), "commandList_->Close failed");
    isCommandListRecording_ = false;

    ID3D12CommandList *lists[] = {commandList_.Get()};
    commandQueue_->ExecuteCommandLists(1, lists);

    WaitForGpu();
}

void DirectXCommon::WaitForGpu() {
    fenceValue_++;
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(), fenceValue_),
                  "commandQueue_->Signal failed");

    if (fence_->GetCompletedValue() < fenceValue_) {
        ThrowIfFailed(fence_->SetEventOnCompletion(fenceValue_, fenceEvent_),
                      "fence_->SetEventOnCompletion failed");
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
    for (UINT i = 0; i < kSwapChainBufferCount; ++i) {
        frameFenceValues_[i] = fenceValue_;
    }
}

void DirectXCommon::SetBackBufferRenderTarget(bool clear, bool bindDepth) {
    auto rtvHandle = GetBackBufferRtvHandle();

    auto dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE *dsvHandlePtr =
        bindDepth ? &dsvHandle : nullptr;

    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissorRect_);
    commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, dsvHandlePtr);

    if (clear) {
        commandList_->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);
        if (bindDepth) {
            commandList_->ClearDepthStencilView(
                dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        }
    }
}

void DirectXCommon::CreateDepthStencilSrv(SrvManager *srvManager) {
    srvManager_ = srvManager;
    depthSrvIndex_ = srvManager_->Allocate();
    depthSrvGpuHandle_ = srvManager_->GetGpuHandle(depthSrvIndex_);
    UpdateDepthStencilSrv();
}

void DirectXCommon::RegisterSceneColorSRV(SrvManager *srvManager) {
    if (srvManager == nullptr) {
        throw std::runtime_error("RegisterSceneColorSRV: srvManager is null");
    }

    srvManager_ = srvManager;
    if (sceneSrvIndex_ == UINT_MAX) {
        sceneSrvIndex_ = srvManager_->Allocate();
    }
    UpdateSceneColorSrv();
}

void DirectXCommon::TransitionDepthToShaderResource() {
    if (!depthBuffer_ ||
        depthState_ == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        depthBuffer_.Get(), depthState_,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList_->ResourceBarrier(1, &barrier);
    depthState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void DirectXCommon::TransitionDepthToWrite() {
    if (!depthBuffer_ || depthState_ == D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        depthBuffer_.Get(), depthState_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    commandList_->ResourceBarrier(1, &barrier);
    depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}

void DirectXCommon::CreateFactory() {
    ThrowIfFailed(CreateDXGIFactory(IID_PPV_ARGS(&factory_)),
                  "CreateDXGIFactory failed");
}

void DirectXCommon::CreateDevice() {
    ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&device_)),
                  "D3D12CreateDevice failed");
}

void DirectXCommon::CreateCommandQueue() {
    D3D12_COMMAND_QUEUE_DESC desc{};
    ThrowIfFailed(
        device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue_)),
        "CreateCommandQueue failed");
}

void DirectXCommon::CreateCommandAllocator() {
    for (UINT i = 0; i < kSwapChainBufferCount; ++i) {
        ThrowIfFailed(
            device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&commandAllocators_[i])),
            "CreateCommandAllocator failed");
    }
}

void DirectXCommon::CreateCommandList() {
    ThrowIfFailed(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             commandAllocators_[backBufferIndex_].Get(),
                                             nullptr,
                                             IID_PPV_ARGS(&commandList_)),
                  "CreateCommandList failed");

    ThrowIfFailed(commandList_->Close(), "commandList_->Close failed");
}

void DirectXCommon::CreateSwapChain(HWND hwnd, int width, int height) {
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = kBackBufferFormat;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kSwapChainBufferCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> sc1;
    ThrowIfFailed(factory_->CreateSwapChainForHwnd(
                      commandQueue_.Get(), hwnd, &desc, nullptr, nullptr, &sc1),
                  "CreateSwapChainForHwnd failed");

    ThrowIfFailed(sc1.As(&swapChain_), "SwapChain As() failed");

    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
}

void DirectXCommon::CreateRTV() {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = kSwapChainBufferCount + 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ThrowIfFailed(
        device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap_)),
        "CreateDescriptorHeap(RTV) failed");

    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
        rtvHeap_->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < kSwapChainBufferCount; i++) {
        ThrowIfFailed(swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i])),
                      "swapChain_->GetBuffer failed");

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Format = kBackBufferFormat;

        device_->CreateRenderTargetView(backBuffers_[i].Get(), &rtvDesc,
                                        handle);

        handle.Offset(1, rtvDescriptorSize_);
    }
}

void DirectXCommon::CreateSceneRenderTarget(int width, int height) {
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
    clearValue.Color[0] = kClearColor[0];
    clearValue.Color[1] = kClearColor[1];
    clearValue.Color[2] = kClearColor[2];
    clearValue.Color[3] = kClearColor[3];

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(device_->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
                      IID_PPV_ARGS(&sceneColorBuffer_)),
                  "CreateCommittedResource(SceneRenderTarget) failed");

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Format = kSceneColorFormat;

    device_->CreateRenderTargetView(sceneColorBuffer_.Get(), &rtvDesc,
                                    GetSceneRtvHandle());
}

void DirectXCommon::CreateViewport(int width, int height) {
    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.Width = static_cast<float>(width);
    viewport_.Height = static_cast<float>(height);
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;
}

void DirectXCommon::CreateScissor(int width, int height) {
    scissorRect_.left = 0;
    scissorRect_.top = 0;
    scissorRect_.right = width;
    scissorRect_.bottom = height;
}

void DirectXCommon::CreateDepthStencil(int width, int height) {
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 1;

    ThrowIfFailed(
        device_->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap_)),
        "CreateDescriptorHeap(DSV) failed");

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

    ThrowIfFailed(device_->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                      D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
                      IID_PPV_ARGS(&depthBuffer_)),
                  "CreateCommittedResource(DepthStencil) failed");

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDescView{};
    dsvDescView.Format = kDepthStencilFormat;
    dsvDescView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    device_->CreateDepthStencilView(
        depthBuffer_.Get(), &dsvDescView,
        dsvHeap_->GetCPUDescriptorHandleForHeapStart());
    depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}

void DirectXCommon::UpdateDepthStencilSrv() {
    if (!srvManager_ || depthSrvIndex_ == UINT_MAX || !depthBuffer_) {
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = kDepthSrvFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    device_->CreateShaderResourceView(
        depthBuffer_.Get(), &srvDesc,
        srvManager_->GetCpuHandle(depthSrvIndex_));
}

void DirectXCommon::UpdateSceneColorSrv() {
    if (!srvManager_ || sceneSrvIndex_ == UINT_MAX || !sceneColorBuffer_) {
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = kSceneColorFormat;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    device_->CreateShaderResourceView(
        sceneColorBuffer_.Get(), &srvDesc,
        srvManager_->GetCpuHandle(sceneSrvIndex_));
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetBackBufferRtvHandle() const {
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        rtvHeap_->GetCPUDescriptorHandleForHeapStart(),
        static_cast<INT>(backBufferIndex_),
        static_cast<INT>(rtvDescriptorSize_));
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetSceneRtvHandle() const {
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        rtvHeap_->GetCPUDescriptorHandleForHeapStart(),
        static_cast<INT>(kSceneRtvIndex), static_cast<INT>(rtvDescriptorSize_));
}

D3D12_GPU_DESCRIPTOR_HANDLE
DirectXCommon::GetSceneSrvGpuHandle(const SrvManager *srvManager) const {
    return srvManager->GetGpuHandle(sceneSrvIndex_);
}

void DirectXCommon::CreateFence() {
    ThrowIfFailed(
        device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
        "CreateFence failed");

    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) {
        throw std::runtime_error("CreateEvent failed");
    }
}

void DirectXCommon::WaitForFrame(UINT frameIndex) {
    if (frameIndex >= kSwapChainBufferCount) {
        return;
    }

    const UINT64 fenceValue = frameFenceValues_[frameIndex];
    if (fenceValue == 0 || fence_->GetCompletedValue() >= fenceValue) {
        return;
    }

    ThrowIfFailed(fence_->SetEventOnCompletion(fenceValue, fenceEvent_),
                  "fence_->SetEventOnCompletion failed");
    WaitForSingleObject(fenceEvent_, INFINITE);
}

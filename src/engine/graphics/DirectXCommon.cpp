#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include <stdexcept>

using namespace DxUtils;

void DirectXCommon::Initialize(HWND hwnd, int width, int height) {
    CreateFactory();
    CreateDevice();
    CreateCommandQueue();
    CreateCommandAllocator();
    CreateCommandList();
    CreateSwapChain(hwnd, width, height);
    CreateRTV();
    CreateViewport(width, height);
    CreateScissor(width, height);
    CreateDepthStencil(width, height);
    CreateFence();
}

void DirectXCommon::BeginFrame() {
    ThrowIfFailed(commandAllocator_->Reset(),
                  "commandAllocator_->Reset failed");
    ThrowIfFailed(commandList_->Reset(commandAllocator_.Get(), nullptr),
                  "commandList_->Reset failed");

    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissorRect_);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffers_[backBufferIndex_].Get(), D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList_->ResourceBarrier(1, &barrier);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        rtvHeap_->GetCPUDescriptorHandleForHeapStart(),
        static_cast<INT>(backBufferIndex_),
        static_cast<INT>(rtvDescriptorSize_));

    auto dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

    commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    commandList_->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f,
                                        0, 0, nullptr);
}

void DirectXCommon::EndFrame() {
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffers_[backBufferIndex_].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    commandList_->ResourceBarrier(1, &barrier);

    ThrowIfFailed(commandList_->Close(), "commandList_->Close failed");

    ID3D12CommandList *lists[] = {commandList_.Get()};
    commandQueue_->ExecuteCommandLists(1, lists);

    ThrowIfFailed(swapChain_->Present(1, 0), "swapChain_->Present failed");

    fenceValue_++;
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(), fenceValue_),
                  "commandQueue_->Signal failed");

    if (fence_->GetCompletedValue() < fenceValue_) {
        ThrowIfFailed(fence_->SetEventOnCompletion(fenceValue_, fenceEvent_),
                      "fence_->SetEventOnCompletion failed");
        WaitForSingleObject(fenceEvent_, INFINITE);
    }

    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
}

void DirectXCommon::BeginUpload() {
    ThrowIfFailed(commandAllocator_->Reset(),
                  "commandAllocator_->Reset failed");

    ThrowIfFailed(commandList_->Reset(commandAllocator_.Get(), nullptr),
                  "commandList_->Reset failed");
}

void DirectXCommon::EndUpload() {
    ThrowIfFailed(commandList_->Close(), "commandList_->Close failed");

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
    ThrowIfFailed(
        device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        IID_PPV_ARGS(&commandAllocator_)),
        "CreateCommandAllocator failed");
}

void DirectXCommon::CreateCommandList() {
    ThrowIfFailed(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             commandAllocator_.Get(), nullptr,
                                             IID_PPV_ARGS(&commandList_)),
                  "CreateCommandList failed");

    ThrowIfFailed(commandList_->Close(), "commandList_->Close failed");
}

void DirectXCommon::CreateSwapChain(HWND hwnd, int width, int height) {
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
    heapDesc.NumDescriptors = kSwapChainBufferCount;
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
        rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

        device_->CreateRenderTargetView(backBuffers_[i].Get(), &rtvDesc,
                                        handle);

        handle.Offset(1, rtvDescriptorSize_);
    }
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
    resDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    resDesc.SampleDesc.Count = 1;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(device_->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                      D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
                      IID_PPV_ARGS(&depthBuffer_)),
                  "CreateCommittedResource(DepthStencil) failed");

    device_->CreateDepthStencilView(
        depthBuffer_.Get(), nullptr,
        dsvHeap_->GetCPUDescriptorHandleForHeapStart());
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

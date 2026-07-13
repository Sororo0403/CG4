#pragma once
#include "DirectXCommonDiagnostics.h"
#include "DirectXCommonInternal.h"
#include "graphics/DirectXCommon.h"
#include "graphics/FrameRollbackRegistry.h"

#include <array>
#include <memory>
#include <wrl.h>

struct DirectXCommon::State {
    struct FrameResourceStateSnapshot {
        std::array<D3D12_RESOURCE_STATES, DirectXCommon::kSwapChainBufferCount> backBufferStates{};
        D3D12_RESOURCE_STATES sceneColorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_STATES depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        bool valid = false;
    };

    float clearColor[4] = {DirectXCommon::kClearColor[0], DirectXCommon::kClearColor[1],
                           DirectXCommon::kClearColor[2], DirectXCommon::kClearColor[3]};

    Microsoft::WRL::ComPtr<IDXGIFactory7> factory;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    DXGI_ADAPTER_DESC1 adapterDesc{};
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>
        commandAllocators[DirectXCommon::kSwapChainBufferCount];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
    bool isCommandListRecording = false;
    bool uploadPassActive = false;
    UINT uploadPassDepth = 0;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> backBuffers[DirectXCommon::kSwapChainBufferCount];
    D3D12_RESOURCE_STATES
    backBufferStates[DirectXCommon::kSwapChainBufferCount]{};
    Microsoft::WRL::ComPtr<ID3D12Resource> sceneColorBuffer;
    D3D12_RESOURCE_STATES sceneColorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    UINT sceneSrvIndex = UINT_MAX;
    UINT rtvDescriptorSize = 0;
    UINT backBufferIndex = 0;

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    UINT64 fenceValue = 0;
    UINT64 frameFenceValues[DirectXCommon::kSwapChainBufferCount]{};
    DirectXCommonInternal::ScopedWin32Handle fenceEvent;
    std::unique_ptr<DirectXCommonGpuDiagnostics> diagnostics =
        std::make_unique<DirectXCommonGpuDiagnostics>();

    D3D12_VIEWPORT sceneViewport{};
    D3D12_RECT sceneScissorRect{};

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer;
    SrvManager* srvManager = nullptr;
    UINT depthSrvIndex = UINT_MAX;
    D3D12_GPU_DESCRIPTOR_HANDLE depthSrvGpuHandle{};
    D3D12_RESOURCE_STATES depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    FrameResourceStateSnapshot frameResourceStateSnapshot{};
    FrameRollbackRegistry frameRollbacks;
};

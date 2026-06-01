#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/SrvManager.h"
#include <algorithm>
#include <cmath>

namespace {
bool LogIfFailed(HRESULT hr, const char *message) {
    if (SUCCEEDED(hr)) {
        return false;
    }
    OutputDebugStringA("DirectXCommon: ");
    OutputDebugStringA(message != nullptr ? message : "HRESULT failed");
    OutputDebugStringA("\n");
    return true;
}

float ClampFinite(float value, float minimum, float maximum, float fallback) {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

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
                                        D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                        nullptr))) {
            adapter = candidate;
            break;
        }
    }

    return adapter;
}
} // namespace

DirectXCommon::~DirectXCommon() {
    WaitForGpuIfPossible();
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

void DirectXCommon::Initialize(HWND hwnd, int width, int height) {
    if (hwnd == nullptr || width <= 0 || height <= 0) {
        return;
    }
    CreateFactory();
    if (!factory_) {
        return;
    }
    CreateDevice();
    if (!device_) {
        return;
    }
    CreateCommandQueue();
    if (!commandQueue_) {
        return;
    }
    CreateCommandAllocator();
    for (const auto &allocator : commandAllocators_) {
        if (!allocator) {
            return;
        }
    }
    CreateCommandList();
    if (!commandList_) {
        return;
    }
    CreateSwapChain(hwnd, width, height);
    if (!swapChain_) {
        return;
    }
    CreateRTV();
    if (!rtvHeap_) {
        return;
    }
    CreateSceneRenderTarget(width, height);
    CreateViewport(width, height);
    CreateScissor(width, height);
    CreateDepthStencil(width, height);
    CreateFence();
}

void DirectXCommon::BeginFrame() {
    ++diagnosticFrameId_;
    TrackGpuPhase("BeginFrame");
    if (!IsInitialized() || !commandList_) {
        return;
    }
    WaitForFrame(backBufferIndex_);
    ID3D12CommandAllocator* commandAllocator =
        commandAllocators_[backBufferIndex_].Get();
    if (commandAllocator == nullptr) {
        return;
    }
    if (LogIfFailed(commandAllocator->Reset(),
                    "commandAllocator_->Reset failed")) {
        return;
    }
    if (LogIfFailed(commandList_->Reset(commandAllocator, nullptr),
                    "commandList_->Reset failed")) {
        return;
    }
    isCommandListRecording_ = true;

    ApplySceneViewportAndScissor();
    TrackGpuPhase("BeginFrame.ResetComplete");
}

void DirectXCommon::BeginScenePass() {
    TrackGpuPhase("BeginScenePass");
    if (!commandList_ || !dsvHeap_ || !depthBuffer_) {
        return;
    }
    TransitionSceneColor(D3D12_RESOURCE_STATE_RENDER_TARGET);

    auto sceneRtv = GetSceneRtvHandle();
    if (sceneRtv.ptr == 0) {
        return;
    }
    auto dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

    ApplySceneViewportAndScissor();
    commandList_->OMSetRenderTargets(1, &sceneRtv, FALSE, &dsvHandle);
    commandList_->ClearRenderTargetView(sceneRtv, clearColor_, 0, nullptr);
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f,
                                        0, 0, nullptr);
}

void DirectXCommon::RestoreSceneRenderState(bool clearDepth) {
    TrackGpuPhase("RestoreSceneRenderState");
    if (!commandList_ || !dsvHeap_ || !depthBuffer_) {
        return;
    }
    TransitionSceneColor(D3D12_RESOURCE_STATE_RENDER_TARGET);

    auto sceneRtv = GetSceneRtvHandle();
    if (sceneRtv.ptr == 0) {
        return;
    }
    auto dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

    ApplySceneViewportAndScissor();
    commandList_->OMSetRenderTargets(1, &sceneRtv, FALSE, &dsvHandle);
    if (clearDepth) {
        commandList_->ClearDepthStencilView(
            dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    }
}

void DirectXCommon::ClearDepth() {
    TrackGpuPhase("ClearDepth");
    if (!commandList_ || !dsvHeap_ || !depthBuffer_) {
        return;
    }
    auto dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH,
                                        1.0f, 0, 0, nullptr);
}

void DirectXCommon::EndScenePass() {
    TrackGpuPhase("EndScenePass");
    TransitionSceneColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void DirectXCommon::BeginBackBufferPass(bool bindDepth) {
    TrackGpuPhase("BeginBackBufferPass");
    TransitionBackBuffer(backBufferIndex_,
                         D3D12_RESOURCE_STATE_RENDER_TARGET);

    SetBackBufferRenderTarget(true, bindDepth);
}

void DirectXCommon::EndFrame() {
    TrackGpuPhase("EndFrame.Begin");
    if (!isCommandListRecording_ || !commandList_ || !commandQueue_ || !device_ ||
        !swapChain_ || !fence_) {
        AbortFrame();
        return;
    }
    TransitionBackBuffer(backBufferIndex_, D3D12_RESOURCE_STATE_PRESENT);

    TrackGpuPhase("EndFrame.CloseCommandList");
    const HRESULT closeResult = commandList_->Close();
    if (FAILED(closeResult)) {
        isCommandListRecording_ = false;
        uploadPassActive_ = false;
        uploadPassDepth_ = 0;
        LogIfFailed(closeResult, "commandList_->Close failed");
        return;
    }
    isCommandListRecording_ = false;

    ID3D12CommandList *lists[] = {commandList_.Get()};
    TrackGpuPhase("EndFrame.ExecuteCommandLists");
    commandQueue_->ExecuteCommandLists(1, lists);

    const UINT submittedBufferIndex = backBufferIndex_;
    fenceValue_++;
    TrackGpuPhase("EndFrame.SignalFence");
    if (LogIfFailed(commandQueue_->Signal(fence_.Get(), fenceValue_),
                    "commandQueue_->Signal failed")) {
        return;
    }
    frameFenceValues_[submittedBufferIndex] = fenceValue_;

    TrackGpuPhase("EndFrame.Present");
    HRESULT presentResult = swapChain_->Present(1, 0);
    if (FAILED(presentResult)) {
        OutputDebugStringA("DirectXCommon: swapChain_->Present failed\n");
        HRESULT removedReason = device_->GetDeviceRemovedReason();
        if (FAILED(removedReason)) {
            OutputDebugStringA("DirectXCommon: D3D12 device removed\n");
            LogIfFailed(removedReason, "D3D12 device removed");
        }
        LogIfFailed(presentResult, "swapChain_->Present failed");
        return;
    }

    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
}

void DirectXCommon::AbortFrame() noexcept {
    if (!isCommandListRecording_) {
        uploadPassActive_ = false;
        uploadPassDepth_ = 0;
        return;
    }

    if (commandList_) {
        commandList_->Close();
    }

    isCommandListRecording_ = false;
    uploadPassActive_ = false;
    uploadPassDepth_ = 0;
}

void DirectXCommon::Resize(int width, int height) {
    TrackGpuPhase("Resize");
    if (!IsInitialized() || !swapChain_ || width <= 0 || height <= 0) {
        return;
    }

    WaitForGpu();

    for (auto &backBuffer : backBuffers_) {
        backBuffer.Reset();
    }
    sceneColorBuffer_.Reset();
    depthBuffer_.Reset();

    if (LogIfFailed(swapChain_->ResizeBuffers(
                        kSwapChainBufferCount, static_cast<UINT>(width),
                        static_cast<UINT>(height), kBackBufferFormat, 0),
                    "swapChain_->ResizeBuffers failed")) {
        return;
    }

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
    TrackGpuPhase("BeginUpload");
    if (!IsInitialized() || !commandList_) {
        return;
    }
    if (isCommandListRecording_) {
        if (uploadPassActive_) {
            ++uploadPassDepth_;
        }
        return;
    }

    WaitForFrame(backBufferIndex_);
    ID3D12CommandAllocator* commandAllocator =
        commandAllocators_[backBufferIndex_].Get();
    if (commandAllocator == nullptr) {
        return;
    }
    if (LogIfFailed(commandAllocator->Reset(),
                    "commandAllocator_->Reset failed")) {
        return;
    }

    if (LogIfFailed(commandList_->Reset(commandAllocator, nullptr),
                    "commandList_->Reset failed")) {
        return;
    }
    isCommandListRecording_ = true;
    uploadPassActive_ = true;
    uploadPassDepth_ = 1;
}

void DirectXCommon::EndUpload() {
    TrackGpuPhase("EndUpload");
    if (!uploadPassActive_) {
        return;
    }
    if (uploadPassDepth_ > 1) {
        --uploadPassDepth_;
        return;
    }

    const HRESULT closeResult = commandList_->Close();
    if (FAILED(closeResult)) {
        isCommandListRecording_ = false;
        uploadPassActive_ = false;
        uploadPassDepth_ = 0;
        LogIfFailed(closeResult, "commandList_->Close failed");
        return;
    }
    isCommandListRecording_ = false;
    uploadPassActive_ = false;
    uploadPassDepth_ = 0;

    ID3D12CommandList *lists[] = {commandList_.Get()};
    TrackGpuPhase("EndUpload.ExecuteCommandLists");
    if (commandQueue_) {
        commandQueue_->ExecuteCommandLists(1, lists);
    }

    WaitForGpu();
}

void DirectXCommon::WaitForGpu() {
    if (!IsInitialized()) {
        return;
    }

    TrackGpuPhase("WaitForGpu");
    fenceValue_++;
    if (LogIfFailed(commandQueue_->Signal(fence_.Get(), fenceValue_),
                    "commandQueue_->Signal failed")) {
        return;
    }

    if (fence_->GetCompletedValue() < fenceValue_) {
        if (LogIfFailed(fence_->SetEventOnCompletion(fenceValue_, fenceEvent_),
                        "fence_->SetEventOnCompletion failed")) {
            return;
        }
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
    for (UINT i = 0; i < kSwapChainBufferCount; ++i) {
        frameFenceValues_[i] = fenceValue_;
    }
}

bool DirectXCommon::WaitForGpuIfPossible() {
    if (!IsInitialized()) {
        return false;
    }

    TrackGpuPhase("WaitForGpuIfPossible");
    ++fenceValue_;
    if (FAILED(commandQueue_->Signal(fence_.Get(), fenceValue_))) {
        return false;
    }

    if (fence_->GetCompletedValue() < fenceValue_) {
        if (FAILED(fence_->SetEventOnCompletion(fenceValue_, fenceEvent_))) {
            return false;
        }
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
    for (UINT i = 0; i < kSwapChainBufferCount; ++i) {
        frameFenceValues_[i] = fenceValue_;
    }
    return true;
}

void DirectXCommon::SetBackBufferRenderTarget(bool clear, bool bindDepth) {
    if (!commandList_) {
        return;
    }
    auto rtvHandle = GetBackBufferRtvHandle();
    if (rtvHandle.ptr == 0) {
        return;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE *dsvHandlePtr = nullptr;
    if (bindDepth) {
        if (!dsvHeap_ || !depthBuffer_) {
            return;
        }
        dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        dsvHandlePtr = &dsvHandle;
    }

    ApplySceneViewportAndScissor();
    commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, dsvHandlePtr);

    if (clear) {
        commandList_->ClearRenderTargetView(rtvHandle, clearColor_, 0,
                                            nullptr);
        if (bindDepth) {
            commandList_->ClearDepthStencilView(
                dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        }
    }
}

void DirectXCommon::ApplySceneViewportAndScissor() {
    if (!commandList_) {
        return;
    }
    commandList_->RSSetViewports(1, &sceneViewport_);
    commandList_->RSSetScissorRects(1, &sceneScissorRect_);
}

void DirectXCommon::TransitionSceneColor(D3D12_RESOURCE_STATES afterState) {
    if (!commandList_ || !sceneColorBuffer_ || sceneColorState_ == afterState) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        sceneColorBuffer_.Get(), sceneColorState_, afterState);
    TrackGpuPhase("TransitionSceneColor");
    commandList_->ResourceBarrier(1, &barrier);
    sceneColorState_ = afterState;
}

void DirectXCommon::TransitionBackBuffer(
    UINT index, D3D12_RESOURCE_STATES afterState) {
    if (!commandList_ || index >= kSwapChainBufferCount || !backBuffers_[index] ||
        backBufferStates_[index] == afterState) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffers_[index].Get(), backBufferStates_[index], afterState);
    TrackGpuPhase("TransitionBackBuffer");
    commandList_->ResourceBarrier(1, &barrier);
    backBufferStates_[index] = afterState;
}

void DirectXCommon::CreateDepthStencilSrv(SrvManager *srvManager) {
    if (srvManager == nullptr) {
        return;
    }

    srvManager_ = srvManager;
    if (depthSrvIndex_ == UINT_MAX) {
        if (!srvManager_->CanAllocate()) {
            depthSrvGpuHandle_ = {};
            return;
        }
        depthSrvIndex_ = srvManager_->Allocate();
    }
    if (depthSrvIndex_ == UINT_MAX) {
        depthSrvGpuHandle_ = {};
        return;
    }
    depthSrvGpuHandle_ = srvManager_->GetGpuHandle(depthSrvIndex_);
    UpdateDepthStencilSrv();
}

void DirectXCommon::RegisterSceneColorSRV(SrvManager *srvManager) {
    if (srvManager == nullptr) {
        return;
    }

    srvManager_ = srvManager;
    if (sceneSrvIndex_ == UINT_MAX) {
        if (!srvManager_->CanAllocate()) {
            return;
        }
        sceneSrvIndex_ = srvManager_->Allocate();
    }
    if (sceneSrvIndex_ == UINT_MAX) {
        return;
    }
    UpdateSceneColorSrv();
}

void DirectXCommon::ReleaseRegisteredSrvs() {
    if (srvManager_ == nullptr) {
        depthSrvIndex_ = UINT_MAX;
        sceneSrvIndex_ = UINT_MAX;
        depthSrvGpuHandle_ = {};
        return;
    }

    if (depthSrvIndex_ != UINT_MAX) {
        srvManager_->FreeIfAllocated(depthSrvIndex_);
        depthSrvIndex_ = UINT_MAX;
    }
    if (sceneSrvIndex_ != UINT_MAX) {
        srvManager_->FreeIfAllocated(sceneSrvIndex_);
        sceneSrvIndex_ = UINT_MAX;
    }

    depthSrvGpuHandle_ = {};
    srvManager_ = nullptr;
}

void DirectXCommon::TransitionDepthToShaderResource() {
    if (!commandList_ || !depthBuffer_ ||
        depthState_ == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        depthBuffer_.Get(), depthState_,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TrackGpuPhase("TransitionDepthToShaderResource");
    commandList_->ResourceBarrier(1, &barrier);
    depthState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void DirectXCommon::TransitionDepthToWrite() {
    if (!commandList_ || !depthBuffer_ ||
        depthState_ == D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        depthBuffer_.Get(), depthState_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    TrackGpuPhase("TransitionDepthToWrite");
    commandList_->ResourceBarrier(1, &barrier);
    depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}

void DirectXCommon::SetClearColor(const DirectX::XMFLOAT4 &color) {
    SetClearColor(color.x, color.y, color.z, color.w);
}

void DirectXCommon::SetClearColor(float r, float g, float b, float a) {
    clearColor_[0] = ClampFinite(r, 0.0f, 1.0f, kClearColor[0]);
    clearColor_[1] = ClampFinite(g, 0.0f, 1.0f, kClearColor[1]);
    clearColor_[2] = ClampFinite(b, 0.0f, 1.0f, kClearColor[2]);
    clearColor_[3] = ClampFinite(a, 0.0f, 1.0f, kClearColor[3]);
}

void DirectXCommon::ResetClearColor() {
    clearColor_[0] = kClearColor[0];
    clearColor_[1] = kClearColor[1];
    clearColor_[2] = kClearColor[2];
    clearColor_[3] = kClearColor[3];
}

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
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = kSwapChainBufferCount + 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (LogIfFailed(
            device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap_)),
            "CreateDescriptorHeap(RTV) failed") ||
        !rtvHeap_) {
        rtvHeap_.Reset();
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
            backBuffers_[i].Reset();
            rtvHeap_.Reset();
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
    if (!device_ || !rtvHeap_ || width <= 0 || height <= 0) {
        sceneColorBuffer_.Reset();
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
        sceneColorBuffer_.Reset();
        return;
    }
    sceneColorBuffer_->SetName(L"DirectXCommon.SceneColorBuffer");
    sceneColorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Format = kSceneColorFormat;

    const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = GetSceneRtvHandle();
    if (sceneRtv.ptr != 0) {
        device_->CreateRenderTargetView(sceneColorBuffer_.Get(), &rtvDesc,
                                        sceneRtv);
    }
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
    if (!device_ || width <= 0 || height <= 0) {
        dsvHeap_.Reset();
        depthBuffer_.Reset();
        return;
    }
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 1;

    if (LogIfFailed(
            device_->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap_)),
            "CreateDescriptorHeap(DSV) failed") ||
        !dsvHeap_) {
        dsvHeap_.Reset();
        depthBuffer_.Reset();
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
        depthBuffer_.Reset();
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
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = kDepthSrvFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    const D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
        srvManager_->GetCpuHandle(depthSrvIndex_);
    if (srvHandle.ptr == 0) {
        depthSrvGpuHandle_ = {};
        return;
    }
    device_->CreateShaderResourceView(depthBuffer_.Get(), &srvDesc, srvHandle);
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

void DirectXCommon::CreateFence() {
    if (!device_) {
        return;
    }
    if (LogIfFailed(
            device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                 IID_PPV_ARGS(&fence_)),
            "CreateFence failed") ||
        !fence_) {
        fence_.Reset();
        return;
    }
    fence_->SetName(L"DirectXCommon.FrameFence");

    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) {
        OutputDebugStringA("DirectXCommon: CreateEvent failed\n");
        fence_.Reset();
    }
}

void DirectXCommon::WaitForFrame(UINT frameIndex) {
    if (!fence_ || fenceEvent_ == nullptr ||
        frameIndex >= kSwapChainBufferCount) {
        return;
    }

    const UINT64 fenceValue = frameFenceValues_[frameIndex];
    if (fenceValue == 0 || fence_->GetCompletedValue() >= fenceValue) {
        return;
    }

    if (LogIfFailed(fence_->SetEventOnCompletion(fenceValue, fenceEvent_),
                    "fence_->SetEventOnCompletion failed")) {
        return;
    }
    WaitForSingleObject(fenceEvent_, INFINITE);
}

void DirectXCommon::TrackGpuPhase(const char *phase) {
    recentGpuPhases_[recentGpuPhaseCursor_] = phase;
    recentGpuPhaseCursor_ =
        (recentGpuPhaseCursor_ + 1) % kRecentGpuPhaseCount;
    if (recentGpuPhaseSize_ < kRecentGpuPhaseCount) {
        ++recentGpuPhaseSize_;
    }
}

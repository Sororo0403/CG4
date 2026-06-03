#include "graphics/DirectXCommon.h"
#include "DirectXCommonInternal.h"
#include "core/Numeric.h"
#include "graphics/DxHelpers.h"
#include "graphics/SrvManager.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
using DirectXCommonInternal::LogIfFailed;
using Numeric::ClampFinite;

constexpr size_t kDefaultFrameRollbackReserve = 32;
} // namespace

DirectXCommon::~DirectXCommon() {
    WaitForGpuIfPossible();
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

bool DirectXCommon::Initialize(HWND hwnd, int width, int height) {
    if (hwnd == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    CreateFactory();
    if (!factory_) {
        return false;
    }
    CreateDevice();
    if (!device_) {
        return false;
    }
    CreateCommandQueue();
    if (!commandQueue_) {
        return false;
    }
    CreateCommandAllocator();
    for (const auto &allocator : commandAllocators_) {
        if (!allocator) {
            return false;
        }
    }
    CreateCommandList();
    if (!commandList_) {
        return false;
    }
    CreateSwapChain(hwnd, width, height);
    if (!swapChain_) {
        return false;
    }
    CreateRTV();
    CreateSceneRenderTarget(width, height);
    CreateViewport(width, height);
    CreateScissor(width, height);
    CreateDepthStencil(width, height);
    if (!HasFrameResources()) {
        return false;
    }
    CreateFence();
    return IsReadyForRendering();
}

void DirectXCommon::BeginFrame() {
    ++diagnosticFrameId_;
    TrackGpuPhase("BeginFrame");
    if (!IsInitialized() || !commandList_ || !HasFrameResources()) {
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
    ClearFrameRollbacks();
    ReserveFrameRollbacks(kDefaultFrameRollbackReserve);
    SnapshotFrameResourceStates();

    ApplySceneViewportAndScissor();
    TrackGpuPhase("BeginFrame.ResetComplete");
}
void DirectXCommon::BeginScenePass() {
    TrackGpuPhase("BeginScenePass");
    ID3D12GraphicsCommandList *commandList = GetCommandList();
    if (!commandList || !dsvHeap_ || !depthBuffer_) {
        return;
    }
    TransitionSceneColor(D3D12_RESOURCE_STATE_RENDER_TARGET);

    auto sceneRtv = GetSceneRtvHandle();
    if (sceneRtv.ptr == 0) {
        return;
    }
    auto dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

    ApplySceneViewportAndScissor();
    commandList->OMSetRenderTargets(1, &sceneRtv, FALSE, &dsvHandle);
    commandList->ClearRenderTargetView(sceneRtv, clearColor_, 0, nullptr);
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f,
                                       0, 0, nullptr);
}

void DirectXCommon::RestoreSceneRenderState(bool clearDepth) {
    TrackGpuPhase("RestoreSceneRenderState");
    ID3D12GraphicsCommandList *commandList = GetCommandList();
    if (!commandList || !dsvHeap_ || !depthBuffer_) {
        return;
    }
    TransitionSceneColor(D3D12_RESOURCE_STATE_RENDER_TARGET);

    auto sceneRtv = GetSceneRtvHandle();
    if (sceneRtv.ptr == 0) {
        return;
    }
    auto dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

    ApplySceneViewportAndScissor();
    commandList->OMSetRenderTargets(1, &sceneRtv, FALSE, &dsvHandle);
    if (clearDepth) {
        commandList->ClearDepthStencilView(
            dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    }
}

void DirectXCommon::ClearDepth() {
    TrackGpuPhase("ClearDepth");
    ID3D12GraphicsCommandList *commandList = GetCommandList();
    if (!commandList || !dsvHeap_ || !depthBuffer_) {
        return;
    }
    auto dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH,
                                       1.0f, 0, 0, nullptr);
}

void DirectXCommon::EndScenePass() {
    TrackGpuPhase("EndScenePass");
    if (!GetCommandList()) {
        return;
    }
    TransitionSceneColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void DirectXCommon::BeginBackBufferPass(bool bindDepth) {
    TrackGpuPhase("BeginBackBufferPass");
    if (!GetCommandList()) {
        return;
    }
    TransitionBackBuffer(backBufferIndex_,
                         D3D12_RESOURCE_STATE_RENDER_TARGET);

    SetBackBufferRenderTarget(true, bindDepth);
}

bool DirectXCommon::EndFrame() {
    TrackGpuPhase("EndFrame.Begin");
    if (!isCommandListRecording_ || !commandList_ || !commandQueue_ || !device_ ||
        !swapChain_ || !fence_) {
        AbortFrame();
        return false;
    }
    TransitionBackBuffer(backBufferIndex_, D3D12_RESOURCE_STATE_PRESENT);

    TrackGpuPhase("EndFrame.CloseCommandList");
    const HRESULT closeResult = commandList_->Close();
    if (FAILED(closeResult)) {
        isCommandListRecording_ = false;
        uploadPassActive_ = false;
        uploadPassDepth_ = 0;
        RestoreFrameRollbacks();
        RestoreFrameResourceStates();
        LogIfFailed(closeResult, "commandList_->Close failed");
        return false;
    }
    isCommandListRecording_ = false;

    ID3D12CommandList *lists[] = {commandList_.Get()};
    TrackGpuPhase("EndFrame.ExecuteCommandLists");
    commandQueue_->ExecuteCommandLists(1, lists);
    ClearFrameRollbacks();
    ClearFrameResourceStateSnapshot();

    const UINT submittedBufferIndex = backBufferIndex_;
    fenceValue_++;
    TrackGpuPhase("EndFrame.SignalFence");
    if (LogIfFailed(commandQueue_->Signal(fence_.Get(), fenceValue_),
                    "commandQueue_->Signal failed")) {
        return false;
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
        return false;
    }

    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
    ClearFrameResourceStateSnapshot();
    return true;
}
void DirectXCommon::AbortFrame() noexcept {
    if (!isCommandListRecording_) {
        uploadPassActive_ = false;
        uploadPassDepth_ = 0;
        RestoreFrameRollbacks();
        RestoreFrameResourceStates();
        return;
    }

    if (commandList_) {
        commandList_->Close();
    }

    isCommandListRecording_ = false;
    uploadPassActive_ = false;
    uploadPassDepth_ = 0;
    RestoreFrameRollbacks();
    RestoreFrameResourceStates();
}

bool DirectXCommon::Resize(int width, int height) {
    TrackGpuPhase("Resize");
    if (!IsInitialized() || !swapChain_ || width <= 0 || height <= 0) {
        return false;
    }

    if (!WaitForGpu()) {
        return false;
    }
    ClearFrameRollbacks();
    ClearFrameResourceStateSnapshot();

    for (auto &backBuffer : backBuffers_) {
        backBuffer.Reset();
    }
    sceneColorBuffer_.Reset();
    depthBuffer_.Reset();

    if (LogIfFailed(swapChain_->ResizeBuffers(
                        kSwapChainBufferCount, static_cast<UINT>(width),
                        static_cast<UINT>(height), kBackBufferFormat, 0),
                    "swapChain_->ResizeBuffers failed")) {
        return false;
    }

    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

    CreateRTV();
    if (!rtvHeap_ || rtvDescriptorSize_ == 0) {
        return false;
    }
    for (const auto &backBuffer : backBuffers_) {
        if (!backBuffer) {
            return false;
        }
    }

    CreateSceneRenderTarget(width, height);
    if (!sceneColorBuffer_) {
        return false;
    }
    CreateViewport(width, height);
    CreateScissor(width, height);
    CreateDepthStencil(width, height);
    if (!dsvHeap_ || !depthBuffer_) {
        depthSrvGpuHandle_ = {};
        return false;
    }
    UpdateDepthStencilSrv();
    UpdateSceneColorSrv();
    return true;
}

bool DirectXCommon::BeginUpload() {
    TrackGpuPhase("BeginUpload");
    if (!IsInitialized() || !commandList_) {
        return false;
    }
    if (isCommandListRecording_) {
        if (uploadPassActive_) {
            ++uploadPassDepth_;
            return true;
        }
        return false;
    }

    WaitForFrame(backBufferIndex_);
    ID3D12CommandAllocator* commandAllocator =
        commandAllocators_[backBufferIndex_].Get();
    if (commandAllocator == nullptr) {
        return false;
    }
    if (LogIfFailed(commandAllocator->Reset(),
                    "commandAllocator_->Reset failed")) {
        return false;
    }

    if (LogIfFailed(commandList_->Reset(commandAllocator, nullptr),
                    "commandList_->Reset failed")) {
        return false;
    }
    isCommandListRecording_ = true;
    uploadPassActive_ = true;
    uploadPassDepth_ = 1;
    ClearFrameRollbacks();
    ReserveFrameRollbacks(kDefaultFrameRollbackReserve);
    return true;
}

DirectXCommon::UploadPassResult DirectXCommon::EndUploadPass() {
    TrackGpuPhase("EndUpload");
    if (!uploadPassActive_) {
        return UploadPassResult::Failed;
    }
    if (uploadPassDepth_ > 1) {
        --uploadPassDepth_;
        return UploadPassResult::Completed;
    }

    const HRESULT closeResult = commandList_->Close();
    if (FAILED(closeResult)) {
        isCommandListRecording_ = false;
        uploadPassActive_ = false;
        uploadPassDepth_ = 0;
        RestoreFrameRollbacks();
        LogIfFailed(closeResult, "commandList_->Close failed");
        return UploadPassResult::Failed;
    }
    isCommandListRecording_ = false;
    uploadPassActive_ = false;
    uploadPassDepth_ = 0;

    ID3D12CommandList *lists[] = {commandList_.Get()};
    TrackGpuPhase("EndUpload.ExecuteCommandLists");
    if (!commandQueue_) {
        RestoreFrameRollbacks();
        return UploadPassResult::Failed;
    }
    commandQueue_->ExecuteCommandLists(1, lists);
    ClearFrameRollbacks();

    if (!WaitForGpuIfPossible()) {
        OutputDebugStringA(
            "DirectXCommon: EndUpload wait failed after command submission\n");
        return UploadPassResult::Submitted;
    }
    return UploadPassResult::Completed;
}

bool DirectXCommon::EndUpload() {
    return EndUploadPass() == UploadPassResult::Completed;
}

bool DirectXCommon::WaitForGpu() {
    if (!IsInitialized()) {
        return false;
    }

    TrackGpuPhase("WaitForGpu");
    fenceValue_++;
    if (LogIfFailed(commandQueue_->Signal(fence_.Get(), fenceValue_),
                    "commandQueue_->Signal failed")) {
        return false;
    }

    if (fence_->GetCompletedValue() < fenceValue_) {
        if (LogIfFailed(fence_->SetEventOnCompletion(fenceValue_, fenceEvent_),
                        "fence_->SetEventOnCompletion failed")) {
            return false;
        }
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
    for (UINT i = 0; i < kSwapChainBufferCount; ++i) {
        frameFenceValues_[i] = fenceValue_;
    }
    return true;
}

bool DirectXCommon::WaitForGpuIfPossible() {
    return WaitForGpu();
}

void DirectXCommon::SetBackBufferRenderTarget(bool clear, bool bindDepth) {
    ID3D12GraphicsCommandList *commandList = GetCommandList();
    if (!commandList) {
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
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, dsvHandlePtr);

    if (clear) {
        commandList->ClearRenderTargetView(rtvHandle, clearColor_, 0, nullptr);
        if (bindDepth) {
            commandList->ClearDepthStencilView(
                dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        }
    }
}

void DirectXCommon::ApplySceneViewportAndScissor() {
    ID3D12GraphicsCommandList *commandList = GetCommandList();
    if (!commandList) {
        return;
    }
    commandList->RSSetViewports(1, &sceneViewport_);
    commandList->RSSetScissorRects(1, &sceneScissorRect_);
}

void DirectXCommon::TransitionSceneColor(D3D12_RESOURCE_STATES afterState) {
    ID3D12GraphicsCommandList *commandList = GetCommandList();
    if (!commandList || !sceneColorBuffer_ || sceneColorState_ == afterState) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        sceneColorBuffer_.Get(), sceneColorState_, afterState);
    TrackGpuPhase("TransitionSceneColor");
    commandList->ResourceBarrier(1, &barrier);
    sceneColorState_ = afterState;
}

void DirectXCommon::TransitionBackBuffer(
    UINT index, D3D12_RESOURCE_STATES afterState) {
    ID3D12GraphicsCommandList *commandList = GetCommandList();
    if (!commandList || index >= kSwapChainBufferCount || !backBuffers_[index] ||
        backBufferStates_[index] == afterState) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffers_[index].Get(), backBufferStates_[index], afterState);
    TrackGpuPhase("TransitionBackBuffer");
    commandList->ResourceBarrier(1, &barrier);
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
    ID3D12GraphicsCommandList *commandList = GetCommandList();
    constexpr D3D12_RESOURCE_STATES shaderReadState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if (!commandList || !depthBuffer_ ||
        depthState_ == shaderReadState) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        depthBuffer_.Get(), depthState_, shaderReadState);
    TrackGpuPhase("TransitionDepthToShaderResource");
    commandList->ResourceBarrier(1, &barrier);
    depthState_ = shaderReadState;
}

void DirectXCommon::TransitionDepthToWrite() {
    ID3D12GraphicsCommandList *commandList = GetCommandList();
    if (!commandList || !depthBuffer_ ||
        depthState_ == D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        depthBuffer_.Get(), depthState_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    TrackGpuPhase("TransitionDepthToWrite");
    commandList->ResourceBarrier(1, &barrier);
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
bool DirectXCommon::ReserveFrameRollbacks(size_t additional) {
    if (!isCommandListRecording_ || additional == 0) {
        return true;
    }
    return frameRollbacks_.ReserveAdditional(additional);
}

bool DirectXCommon::RegisterFrameRollback(std::function<void()> rollback) {
    return RegisterFrameRollback(nullptr, std::move(rollback));
}

bool DirectXCommon::RegisterFrameRollback(
    const void *owner, std::function<void()> rollback) {
    if (!isCommandListRecording_ || !rollback) {
        return true;
    }
    return frameRollbacks_.Add(owner, std::move(rollback));
}

void DirectXCommon::UnregisterFrameRollbacks(const void *owner) noexcept {
    frameRollbacks_.RemoveOwner(owner);
}

void DirectXCommon::SnapshotFrameResourceStates() {
    for (UINT index = 0; index < kSwapChainBufferCount; ++index) {
        frameBackBufferStates_[index] = backBufferStates_[index];
    }
    frameSceneColorState_ = sceneColorState_;
    frameDepthState_ = depthState_;
    hasFrameStateSnapshot_ = true;
}

void DirectXCommon::RestoreFrameResourceStates() noexcept {
    if (!hasFrameStateSnapshot_) {
        return;
    }
    for (UINT index = 0; index < kSwapChainBufferCount; ++index) {
        backBufferStates_[index] = frameBackBufferStates_[index];
    }
    sceneColorState_ = frameSceneColorState_;
    depthState_ = frameDepthState_;
    hasFrameStateSnapshot_ = false;
}

void DirectXCommon::ClearFrameResourceStateSnapshot() noexcept {
    hasFrameStateSnapshot_ = false;
}

void DirectXCommon::RestoreFrameRollbacks() noexcept {
    frameRollbacks_.Restore();
}

void DirectXCommon::ClearFrameRollbacks() noexcept {
    frameRollbacks_.Clear();
}

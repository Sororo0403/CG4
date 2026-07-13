#include "graphics/DirectXCommon.h"

#include "graphics/SrvManager.h"
#include "internal/DirectXCommonDiagnostics.h"
#include "internal/DirectXCommonInternal.h"
#include "internal/DirectXCommonState.h"

#include <algorithm>
#include <iterator>

namespace {
using DirectXCommonInternal::LogIfFailed;

constexpr size_t kDefaultFrameRollbackReserve = 32;
} // namespace

DirectXCommon::DirectXCommon() : state_(std::make_unique<State>()) {}

DirectXCommon::~DirectXCommon() {
    WaitForGpuIfPossible();
}

bool DirectXCommon::IsInitialized() const {
    return state_->commandQueue && state_->fence && static_cast<bool>(state_->fenceEvent);
}

bool DirectXCommon::IsReadyForRendering() const {
    return IsInitialized() && state_->commandList && state_->swapChain && HasFrameResources();
}

ID3D12Device* DirectXCommon::GetDevice() const {
    return state_->device.Get();
}

ID3D12CommandQueue* DirectXCommon::GetCommandQueue() const {
    return state_->commandQueue.Get();
}

ID3D12GraphicsCommandList* DirectXCommon::GetCommandList() const {
    return state_->isCommandListRecording ? state_->commandList.Get() : nullptr;
}

bool DirectXCommon::IsCommandListRecording() const {
    return state_->isCommandListRecording;
}

bool DirectXCommon::IsUploadPassActive() const {
    return state_->uploadPassActive;
}

UINT DirectXCommon::GetBackBufferIndex() const {
    return state_->backBufferIndex;
}

uint32_t DirectXCommon::GetSceneWidth() const {
    return state_->sceneViewport.Width > 0.0f ? static_cast<uint32_t>(state_->sceneViewport.Width)
                                              : 0u;
}

uint32_t DirectXCommon::GetSceneHeight() const {
    return state_->sceneViewport.Height > 0.0f ? static_cast<uint32_t>(state_->sceneViewport.Height)
                                               : 0u;
}

ID3D12Resource* DirectXCommon::GetSceneColorBuffer() const {
    return state_->sceneColorBuffer.Get();
}

UINT DirectXCommon::GetSceneSrvIndex() const {
    return state_->sceneSrvIndex;
}

UINT DirectXCommon::GetAdapterVendorId() const {
    return state_->adapterDesc.VendorId;
}

bool DirectXCommon::IsIntelAdapter() const {
    return state_->adapterDesc.VendorId == 0x8086;
}

std::wstring DirectXCommon::GetAdapterDescription() const {
    return state_->adapterDesc.Description;
}

bool DirectXCommon::Initialize(HWND hwnd, int width, int height) {
    if (hwnd == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    CreateFactory();
    if (!state_->factory) {
        return false;
    }
    CreateDevice();
    if (!state_->device) {
        return false;
    }
    CreateCommandQueue();
    if (!state_->commandQueue) {
        return false;
    }
    CreateCommandAllocator();
    if (!std::all_of(std::begin(state_->commandAllocators), std::end(state_->commandAllocators),
                     [](const auto& allocator) { return allocator != nullptr; })) {
        return false;
    }
    CreateCommandList();
    if (!state_->commandList) {
        return false;
    }
    CreateSwapChain(hwnd, width, height);
    if (!state_->swapChain) {
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
    state_->diagnostics->BeginFrame();
    TrackGpuPhase("BeginFrame");
    if (!IsInitialized() || !state_->commandList || !HasFrameResources()) {
        return;
    }
    WaitForFrame(state_->backBufferIndex);
    ID3D12CommandAllocator* commandAllocator =
        state_->commandAllocators[state_->backBufferIndex].Get();
    if (commandAllocator == nullptr) {
        return;
    }
    if (LogIfFailed(commandAllocator->Reset(), "commandAllocator_->Reset failed")) {
        return;
    }
    if (LogIfFailed(state_->commandList->Reset(commandAllocator, nullptr),
                    "state_->commandList->Reset failed")) {
        return;
    }
    state_->isCommandListRecording = true;
    ClearFrameRollbacks();
    ReserveFrameRollbacks(kDefaultFrameRollbackReserve);
    SnapshotFrameResourceStates();

    ApplySceneViewportAndScissor();
    TrackGpuPhase("BeginFrame.ResetComplete");
}
bool DirectXCommon::EndFrame() {
    TrackGpuPhase("EndFrame.Begin");
    if (!state_->isCommandListRecording || !state_->commandList || !state_->commandQueue ||
        !state_->device || !state_->swapChain || !state_->fence) {
        AbortFrame();
        return false;
    }
    TransitionBackBuffer(state_->backBufferIndex, D3D12_RESOURCE_STATE_PRESENT);

    TrackGpuPhase("EndFrame.CloseCommandList");
    const HRESULT closeResult = state_->commandList->Close();
    if (FAILED(closeResult)) {
        state_->isCommandListRecording = false;
        state_->uploadPassActive = false;
        state_->uploadPassDepth = 0;
        RestoreFrameRollbacks();
        RestoreFrameResourceStates();
        LogIfFailed(closeResult, "state_->commandList->Close failed");
        return false;
    }
    state_->isCommandListRecording = false;

    ID3D12CommandList* lists[] = {state_->commandList.Get()};
    TrackGpuPhase("EndFrame.ExecuteCommandLists");
    state_->commandQueue->ExecuteCommandLists(1, lists);
    ClearFrameRollbacks();
    ClearFrameResourceStateSnapshot();

    const UINT submittedBufferIndex = state_->backBufferIndex;
    state_->fenceValue++;
    TrackGpuPhase("EndFrame.SignalFence");
    if (LogIfFailed(state_->commandQueue->Signal(state_->fence.Get(), state_->fenceValue),
                    "state_->commandQueue->Signal failed")) {
        return false;
    }
    state_->frameFenceValues[submittedBufferIndex] = state_->fenceValue;

    TrackGpuPhase("EndFrame.Present");
    HRESULT presentResult = state_->swapChain->Present(1, 0);
    if (FAILED(presentResult)) {
        OutputDebugStringA("DirectXCommon: state_->swapChain->Present failed\n");
        HRESULT removedReason = state_->device->GetDeviceRemovedReason();
        if (FAILED(removedReason)) {
            OutputDebugStringA("DirectXCommon: D3D12 device removed\n");
            LogIfFailed(removedReason, "D3D12 device removed");
        }
        LogIfFailed(presentResult, "state_->swapChain->Present failed");
        return false;
    }

    state_->backBufferIndex = state_->swapChain->GetCurrentBackBufferIndex();
    ClearFrameResourceStateSnapshot();
    return true;
}
void DirectXCommon::AbortFrame() noexcept {
    if (!state_->isCommandListRecording) {
        state_->uploadPassActive = false;
        state_->uploadPassDepth = 0;
        RestoreFrameRollbacks();
        RestoreFrameResourceStates();
        return;
    }

    if (state_->commandList) {
        state_->commandList->Close();
    }

    state_->isCommandListRecording = false;
    state_->uploadPassActive = false;
    state_->uploadPassDepth = 0;
    RestoreFrameRollbacks();
    RestoreFrameResourceStates();
}

bool DirectXCommon::Resize(int width, int height) {
    TrackGpuPhase("Resize");
    if (!IsInitialized() || !state_->swapChain || width <= 0 || height <= 0) {
        return false;
    }

    if (!WaitForGpu()) {
        return false;
    }
    ClearFrameRollbacks();
    ClearFrameResourceStateSnapshot();

    for (auto& backBuffer : state_->backBuffers) {
        backBuffer.Reset();
    }
    state_->sceneColorBuffer.Reset();
    state_->depthBuffer.Reset();

    if (LogIfFailed(
            state_->swapChain->ResizeBuffers(kSwapChainBufferCount, static_cast<UINT>(width),
                                             static_cast<UINT>(height), kBackBufferFormat, 0),
            "state_->swapChain->ResizeBuffers failed")) {
        return false;
    }

    state_->backBufferIndex = state_->swapChain->GetCurrentBackBufferIndex();

    CreateRTV();
    if (!state_->rtvHeap || state_->rtvDescriptorSize == 0) {
        return false;
    }
    if (!std::all_of(std::begin(state_->backBuffers), std::end(state_->backBuffers),
                     [](const auto& backBuffer) { return backBuffer != nullptr; })) {
        return false;
    }

    CreateSceneRenderTarget(width, height);
    if (!state_->sceneColorBuffer) {
        return false;
    }
    CreateViewport(width, height);
    CreateScissor(width, height);
    CreateDepthStencil(width, height);
    if (!state_->dsvHeap || !state_->depthBuffer) {
        state_->depthSrvGpuHandle = {};
        return false;
    }
    return UpdateDepthStencilSrv() && UpdateSceneColorSrv();
}

bool DirectXCommon::BeginUpload() {
    TrackGpuPhase("BeginUpload");
    if (!IsInitialized() || !state_->commandList) {
        return false;
    }
    if (state_->isCommandListRecording) {
        if (state_->uploadPassActive) {
            ++state_->uploadPassDepth;
            return true;
        }
        return false;
    }

    WaitForFrame(state_->backBufferIndex);
    ID3D12CommandAllocator* commandAllocator =
        state_->commandAllocators[state_->backBufferIndex].Get();
    if (commandAllocator == nullptr) {
        return false;
    }
    if (LogIfFailed(commandAllocator->Reset(), "commandAllocator_->Reset failed")) {
        return false;
    }

    if (LogIfFailed(state_->commandList->Reset(commandAllocator, nullptr),
                    "state_->commandList->Reset failed")) {
        return false;
    }
    state_->isCommandListRecording = true;
    state_->uploadPassActive = true;
    state_->uploadPassDepth = 1;
    ClearFrameRollbacks();
    ReserveFrameRollbacks(kDefaultFrameRollbackReserve);
    return true;
}

DirectXCommon::UploadPassResult DirectXCommon::EndUploadPass() {
    TrackGpuPhase("EndUpload");
    if (!state_->uploadPassActive) {
        return UploadPassResult::Failed;
    }
    if (state_->uploadPassDepth > 1) {
        --state_->uploadPassDepth;
        return UploadPassResult::Completed;
    }

    const HRESULT closeResult = state_->commandList->Close();
    if (FAILED(closeResult)) {
        state_->isCommandListRecording = false;
        state_->uploadPassActive = false;
        state_->uploadPassDepth = 0;
        RestoreFrameRollbacks();
        LogIfFailed(closeResult, "state_->commandList->Close failed");
        return UploadPassResult::Failed;
    }
    state_->isCommandListRecording = false;
    state_->uploadPassActive = false;
    state_->uploadPassDepth = 0;

    ID3D12CommandList* lists[] = {state_->commandList.Get()};
    TrackGpuPhase("EndUpload.ExecuteCommandLists");
    if (!state_->commandQueue) {
        RestoreFrameRollbacks();
        return UploadPassResult::Failed;
    }
    state_->commandQueue->ExecuteCommandLists(1, lists);
    ClearFrameRollbacks();

    if (!WaitForGpuIfPossible()) {
        OutputDebugStringA("DirectXCommon: EndUpload wait failed after command submission\n");
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
    state_->fenceValue++;
    if (LogIfFailed(state_->commandQueue->Signal(state_->fence.Get(), state_->fenceValue),
                    "state_->commandQueue->Signal failed")) {
        return false;
    }

    if (state_->fence->GetCompletedValue() < state_->fenceValue) {
        if (LogIfFailed(
                state_->fence->SetEventOnCompletion(state_->fenceValue, state_->fenceEvent.Get()),
                "state_->fence->SetEventOnCompletion failed")) {
            return false;
        }
        WaitForSingleObject(state_->fenceEvent.Get(), INFINITE);
    }
    for (UINT i = 0; i < kSwapChainBufferCount; ++i) {
        state_->frameFenceValues[i] = state_->fenceValue;
    }
    return true;
}

bool DirectXCommon::WaitForGpuIfPossible() {
    return WaitForGpu();
}

bool DirectXCommon::CreateDepthStencilSrv(SrvManager* srvManager) {
    if (srvManager == nullptr) {
        return false;
    }

    state_->srvManager = srvManager;
    if (state_->depthSrvIndex == UINT_MAX) {
        if (!state_->srvManager->CanAllocate()) {
            state_->depthSrvGpuHandle = {};
            return false;
        }
        state_->depthSrvIndex = state_->srvManager->Allocate();
    }
    if (state_->depthSrvIndex == UINT_MAX) {
        state_->depthSrvGpuHandle = {};
        return false;
    }
    return UpdateDepthStencilSrv();
}

bool DirectXCommon::RegisterSceneColorSRV(SrvManager* srvManager) {
    if (srvManager == nullptr) {
        return false;
    }

    state_->srvManager = srvManager;
    if (state_->sceneSrvIndex == UINT_MAX) {
        if (!state_->srvManager->CanAllocate()) {
            return false;
        }
        state_->sceneSrvIndex = state_->srvManager->Allocate();
    }
    if (state_->sceneSrvIndex == UINT_MAX) {
        return false;
    }
    return UpdateSceneColorSrv();
}

void DirectXCommon::ReleaseRegisteredSrvs() {
    if (state_->srvManager == nullptr) {
        state_->depthSrvIndex = UINT_MAX;
        state_->sceneSrvIndex = UINT_MAX;
        state_->depthSrvGpuHandle = {};
        return;
    }

    if (state_->depthSrvIndex != UINT_MAX) {
        state_->srvManager->FreeIfAllocated(state_->depthSrvIndex);
        state_->depthSrvIndex = UINT_MAX;
    }
    if (state_->sceneSrvIndex != UINT_MAX) {
        state_->srvManager->FreeIfAllocated(state_->sceneSrvIndex);
        state_->sceneSrvIndex = UINT_MAX;
    }

    state_->depthSrvGpuHandle = {};
    state_->srvManager = nullptr;
}

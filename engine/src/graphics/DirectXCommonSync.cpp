#include "graphics/DirectXCommon.h"
#include "DirectXCommonInternal.h"

using DirectXCommonInternal::LogIfFailed;

bool DirectXCommon::HasFrameResources() const {
    if (!swapChain_ || !rtvHeap_ || rtvDescriptorSize_ == 0 ||
        !sceneColorBuffer_ || !dsvHeap_ || !depthBuffer_ ||
        backBufferIndex_ >= kSwapChainBufferCount) {
        return false;
    }

    for (const auto &backBuffer : backBuffers_) {
        if (!backBuffer) {
            return false;
        }
    }

    return true;
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
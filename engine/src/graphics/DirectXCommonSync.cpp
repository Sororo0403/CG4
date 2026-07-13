#include "graphics/DirectXCommon.h"
#include "internal/DirectXCommonDiagnostics.h"
#include "internal/DirectXCommonInternal.h"
#include "internal/DirectXCommonState.h"

#include <algorithm>
#include <iterator>

using DirectXCommonInternal::LogIfFailed;

bool DirectXCommon::HasFrameResources() const {
    if (!state_->swapChain || !state_->rtvHeap || state_->rtvDescriptorSize == 0 ||
        !state_->sceneColorBuffer || !state_->dsvHeap || !state_->depthBuffer ||
        state_->backBufferIndex >= kSwapChainBufferCount) {
        return false;
    }

    return std::all_of(std::begin(state_->backBuffers), std::end(state_->backBuffers),
                       [](const auto& backBuffer) { return backBuffer != nullptr; });
}

void DirectXCommon::CreateFence() {
    if (!state_->device) {
        return;
    }
    if (LogIfFailed(
            state_->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&state_->fence)),
            "CreateFence failed") ||
        !state_->fence) {
        state_->fence.Reset();
        return;
    }
    state_->fence->SetName(L"DirectXCommon.FrameFence");

    state_->fenceEvent.Reset(CreateEvent(nullptr, FALSE, FALSE, nullptr));
    if (!state_->fenceEvent) {
        OutputDebugStringA("DirectXCommon: CreateEvent failed\n");
        state_->fence.Reset();
    }
}

void DirectXCommon::WaitForFrame(UINT frameIndex) {
    if (!state_->fence || !state_->fenceEvent || frameIndex >= kSwapChainBufferCount) {
        return;
    }

    const UINT64 fenceValue = state_->frameFenceValues[frameIndex];
    if (fenceValue == 0 || state_->fence->GetCompletedValue() >= fenceValue) {
        return;
    }

    if (LogIfFailed(state_->fence->SetEventOnCompletion(fenceValue, state_->fenceEvent.Get()),
                    "state_->fence->SetEventOnCompletion failed")) {
        return;
    }
    WaitForSingleObject(state_->fenceEvent.Get(), INFINITE);
}

void DirectXCommon::TrackGpuPhase(const char* phase) {
    state_->diagnostics->TrackPhase(phase);
}

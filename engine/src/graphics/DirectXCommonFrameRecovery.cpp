#include "graphics/DirectXCommon.h"
#include "internal/DirectXCommonState.h"

#include <utility>

bool DirectXCommon::ReserveFrameRollbacks(size_t additional) {
    if (!state_->isCommandListRecording || additional == 0) {
        return true;
    }
    return state_->frameRollbacks.ReserveAdditional(additional);
}

bool DirectXCommon::RegisterFrameRollback(std::function<void()> rollback) {
    return RegisterFrameRollback(nullptr, std::move(rollback));
}

bool DirectXCommon::RegisterFrameRollback(const void* owner, std::function<void()> rollback) {
    if (!state_->isCommandListRecording || !rollback) {
        return true;
    }
    return state_->frameRollbacks.Add(owner, std::move(rollback));
}

void DirectXCommon::UnregisterFrameRollbacks(const void* owner) noexcept {
    state_->frameRollbacks.RemoveOwner(owner);
}

void DirectXCommon::SnapshotFrameResourceStates() {
    for (UINT index = 0; index < kSwapChainBufferCount; ++index) {
        state_->frameResourceStateSnapshot.backBufferStates[index] =
            state_->backBufferStates[index];
    }
    state_->frameResourceStateSnapshot.sceneColorState = state_->sceneColorState;
    state_->frameResourceStateSnapshot.depthState = state_->depthState;
    state_->frameResourceStateSnapshot.valid = true;
}

void DirectXCommon::RestoreFrameResourceStates() noexcept {
    if (!state_->frameResourceStateSnapshot.valid) {
        return;
    }
    for (UINT index = 0; index < kSwapChainBufferCount; ++index) {
        state_->backBufferStates[index] =
            state_->frameResourceStateSnapshot.backBufferStates[index];
    }
    state_->sceneColorState = state_->frameResourceStateSnapshot.sceneColorState;
    state_->depthState = state_->frameResourceStateSnapshot.depthState;
    state_->frameResourceStateSnapshot.valid = false;
}

void DirectXCommon::ClearFrameResourceStateSnapshot() noexcept {
    state_->frameResourceStateSnapshot.valid = false;
}

void DirectXCommon::RestoreFrameRollbacks() noexcept {
    state_->frameRollbacks.Restore();
}

void DirectXCommon::ClearFrameRollbacks() noexcept {
    state_->frameRollbacks.Clear();
}

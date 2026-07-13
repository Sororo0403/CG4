#include "core/Numeric.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "internal/DirectXCommonState.h"

namespace {
using Numeric::ClampFinite;
} // namespace

void DirectXCommon::BeginScenePass() {
    TrackGpuPhase("BeginScenePass");
    TransitionSceneColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    BindSceneRenderTarget(true, true);
}

void DirectXCommon::RestoreSceneRenderState(bool clearDepth) {
    TrackGpuPhase("RestoreSceneRenderState");
    TransitionSceneColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    BindSceneRenderTarget(false, clearDepth);
}

void DirectXCommon::BeginSceneColorOverlayPass() {
    TrackGpuPhase("BeginSceneColorOverlayPass");
    TransitionSceneColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12GraphicsCommandList* commandList = GetCommandList();
    if (!commandList) {
        return;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = GetSceneRtvHandle();
    if (sceneRtv.ptr == 0) {
        return;
    }
    ApplySceneViewportAndScissor();
    commandList->OMSetRenderTargets(1, &sceneRtv, FALSE, nullptr);
}

void DirectXCommon::ClearDepth() {
    TrackGpuPhase("ClearDepth");
    ID3D12GraphicsCommandList* commandList = GetCommandList();
    if (!commandList || !state_->dsvHeap || !state_->depthBuffer) {
        return;
    }
    auto dsvHandle = state_->dsvHeap->GetCPUDescriptorHandleForHeapStart();
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
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
    TransitionBackBuffer(state_->backBufferIndex, D3D12_RESOURCE_STATE_RENDER_TARGET);

    SetBackBufferRenderTarget(true, bindDepth);
}

void DirectXCommon::SetBackBufferRenderTarget(bool clear, bool bindDepth) {
    ID3D12GraphicsCommandList* commandList = GetCommandList();
    if (!commandList) {
        return;
    }
    auto rtvHandle = GetBackBufferRtvHandle();
    if (rtvHandle.ptr == 0) {
        return;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandlePtr = nullptr;
    if (bindDepth) {
        if (!state_->dsvHeap || !state_->depthBuffer) {
            return;
        }
        dsvHandle = state_->dsvHeap->GetCPUDescriptorHandleForHeapStart();
        dsvHandlePtr = &dsvHandle;
    }

    ApplySceneViewportAndScissor();
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, dsvHandlePtr);

    if (clear) {
        commandList->ClearRenderTargetView(rtvHandle, state_->clearColor, 0, nullptr);
        if (bindDepth) {
            commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0,
                                               nullptr);
        }
    }
}

void DirectXCommon::ApplySceneViewportAndScissor() {
    ID3D12GraphicsCommandList* commandList = GetCommandList();
    if (!commandList) {
        return;
    }
    commandList->RSSetViewports(1, &state_->sceneViewport);
    commandList->RSSetScissorRects(1, &state_->sceneScissorRect);
}

void DirectXCommon::BindSceneRenderTarget(bool clearColor, bool clearDepth) {
    ID3D12GraphicsCommandList* commandList = GetCommandList();
    if (!commandList || !state_->dsvHeap || !state_->depthBuffer) {
        return;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = GetSceneRtvHandle();
    if (sceneRtv.ptr == 0) {
        return;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = state_->dsvHeap->GetCPUDescriptorHandleForHeapStart();

    ApplySceneViewportAndScissor();
    commandList->OMSetRenderTargets(1, &sceneRtv, FALSE, &dsvHandle);
    if (clearColor) {
        commandList->ClearRenderTargetView(sceneRtv, state_->clearColor, 0, nullptr);
    }
    if (clearDepth) {
        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    }
}

void DirectXCommon::TransitionSceneColor(D3D12_RESOURCE_STATES afterState) {
    ID3D12GraphicsCommandList* commandList = GetCommandList();
    if (!commandList || !state_->sceneColorBuffer || state_->sceneColorState == afterState) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(state_->sceneColorBuffer.Get(),
                                                        state_->sceneColorState, afterState);
    TrackGpuPhase("TransitionSceneColor");
    commandList->ResourceBarrier(1, &barrier);
    state_->sceneColorState = afterState;
}

void DirectXCommon::TransitionBackBuffer(UINT index, D3D12_RESOURCE_STATES afterState) {
    ID3D12GraphicsCommandList* commandList = GetCommandList();
    if (!commandList || index >= kSwapChainBufferCount || !state_->backBuffers[index] ||
        state_->backBufferStates[index] == afterState) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        state_->backBuffers[index].Get(), state_->backBufferStates[index], afterState);
    TrackGpuPhase("TransitionBackBuffer");
    commandList->ResourceBarrier(1, &barrier);
    state_->backBufferStates[index] = afterState;
}

void DirectXCommon::TransitionDepthToShaderResource() {
    ID3D12GraphicsCommandList* commandList = GetCommandList();
    constexpr D3D12_RESOURCE_STATES shaderReadState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if (!commandList || !state_->depthBuffer || state_->depthState == shaderReadState) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(state_->depthBuffer.Get(),
                                                        state_->depthState, shaderReadState);
    TrackGpuPhase("TransitionDepthToShaderResource");
    commandList->ResourceBarrier(1, &barrier);
    state_->depthState = shaderReadState;
}

void DirectXCommon::TransitionDepthToWrite() {
    ID3D12GraphicsCommandList* commandList = GetCommandList();
    if (!commandList || !state_->depthBuffer ||
        state_->depthState == D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        state_->depthBuffer.Get(), state_->depthState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    TrackGpuPhase("TransitionDepthToWrite");
    commandList->ResourceBarrier(1, &barrier);
    state_->depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}

void DirectXCommon::SetClearColor(const DirectX::XMFLOAT4& color) {
    SetClearColor(color.x, color.y, color.z, color.w);
}

void DirectXCommon::SetClearColor(float r, float g, float b, float a) {
    state_->clearColor[0] = ClampFinite(r, 0.0f, 1.0f, kClearColor[0]);
    state_->clearColor[1] = ClampFinite(g, 0.0f, 1.0f, kClearColor[1]);
    state_->clearColor[2] = ClampFinite(b, 0.0f, 1.0f, kClearColor[2]);
    state_->clearColor[3] = ClampFinite(a, 0.0f, 1.0f, kClearColor[3]);
}

void DirectXCommon::ResetClearColor() {
    state_->clearColor[0] = kClearColor[0];
    state_->clearColor[1] = kClearColor[1];
    state_->clearColor[2] = kClearColor[2];
    state_->clearColor[3] = kClearColor[3];
}

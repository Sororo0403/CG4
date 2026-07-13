#include "graphics/DirectXCommon.h"
#include "graphics/SrvManager.h"
#include "internal/GPUParticleEmitterUtils.h"
#include "internal/GPUParticleSystemInternal.h"
#include "internal/GPUParticleSystemShared.h"
#include "particle/GPUParticleSystem.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
using GpuParticleEmitterUtils::IsContinuousEmitter;
using GpuParticleEmitterUtils::ResolveTextureId;
} // namespace

bool GPUParticleSystem::HasDrawResources() const {
    if (!dxCommon_ || !srvManager_ || !textureManager_ || !dxCommon_->IsCommandListRecording() ||
        !resources_->particleResource || !resources_->activeIndexResource ||
        !resources_->drawArgsResource || !resources_->drawCommandSignature ||
        !resources_->drawRootSignature || !resources_->drawPso || !HasConstantBuffers() ||
        resources_->particleSrvGpuHandle.ptr == 0 || resources_->activeIndexSrvGpuHandle.ptr == 0) {
        return false;
    }
    return true;
}

bool GPUParticleSystem::ShouldSkipDraw() const {
    return !updatePending_ && pendingEmitSettings_.empty() && activeTimeRemaining_ <= 0.0f &&
           !IsContinuousEmitter(emitterSettings_);
}

GPUParticleSystem::ConstantFrame* GPUParticleSystem::PrepareDrawFrame(
    ID3D12GraphicsCommandList*& commandList) {
    if (!BindDescriptorHeap(commandList)) {
        return nullptr;
    }
    ConstantFrame* constantFrame = GetCurrentConstantFrame();
    if (constantFrame == nullptr || !constantFrame->drawConstantBuffer ||
        constantFrame->mappedDrawCB == nullptr) {
        return nullptr;
    }
    return constantFrame;
}

void GPUParticleSystem::UpdateDrawConstants(const Camera& camera, ConstantFrame& constantFrame) const {
    XMMATRIX viewProjection = camera.GetView() * camera.GetProj();
    XMStoreFloat4x4(&constantFrame.mappedDrawCB->viewProjection, XMMatrixTranspose(viewProjection));

    XMMATRIX billboard = camera.GetView();
    billboard.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    const XMVECTOR billboardDeterminant = XMMatrixDeterminant(billboard);
    const float billboardDeterminantValue = XMVectorGetX(billboardDeterminant);
    billboard =
        std::isfinite(billboardDeterminantValue) && std::abs(billboardDeterminantValue) > 0.000001f
            ? XMMatrixInverse(nullptr, billboard)
            : XMMatrixIdentity();

    XMFLOAT3 right{};
    XMFLOAT3 up{};
    XMStoreFloat3(&right, billboard.r[0]);
    XMStoreFloat3(&up, billboard.r[1]);
    constantFrame.mappedDrawCB->cameraRight = {right.x, right.y, right.z, 0.0f};
    constantFrame.mappedDrawCB->cameraUp = {up.x, up.y, up.z, 0.0f};
    constantFrame.mappedDrawCB->tintColor = {1.0f, 1.0f, 1.0f, 1.0f};
    constantFrame.mappedDrawCB->atlasInfo = {
        static_cast<float>((std::max)(1u, emitterSettings_.atlasColumns)),
        static_cast<float>((std::max)(1u, emitterSettings_.atlasRows)), 0.0f, 0.0f};
    constantFrame.mappedDrawCB->materialParams0 = materialSettings_.params0;
    constantFrame.mappedDrawCB->materialParams1 = materialSettings_.params1;
}

bool GPUParticleSystem::ResolveDrawTextureHandles(
    D3D12_GPU_DESCRIPTOR_HANDLE& baseTextureHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE& noiseTextureHandle) const {
    const uint32_t whiteTextureId = textureManager_->GetWhiteTextureId();
    const uint32_t noiseTextureId =
        ResolveTextureId(textureManager_, materialSettings_.noiseTextureId, whiteTextureId);
    const uint32_t baseTextureId = ResolveTextureId(textureManager_, textureId_, whiteTextureId);
    baseTextureHandle = textureManager_->GetGpuHandle(baseTextureId);
    noiseTextureHandle = textureManager_->GetGpuHandle(noiseTextureId);
    return baseTextureHandle.ptr != 0 && noiseTextureHandle.ptr != 0;
}

void GPUParticleSystem::RecordDrawCommands(ID3D12GraphicsCommandList* commandList,
                                           ConstantFrame& constantFrame,
                                           D3D12_GPU_DESCRIPTOR_HANDLE baseTextureHandle,
                                           D3D12_GPU_DESCRIPTOR_HANDLE noiseTextureHandle) {
    commandList->SetGraphicsRootSignature(resources_->drawRootSignature.Get());
    commandList->SetPipelineState(resources_->drawPso.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootConstantBufferView(
        0, constantFrame.drawConstantBuffer->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(1, resources_->particleSrvGpuHandle);
    commandList->SetGraphicsRootDescriptorTable(2, baseTextureHandle);
    commandList->SetGraphicsRootDescriptorTable(3, noiseTextureHandle);
    commandList->SetGraphicsRootDescriptorTable(4, resources_->activeIndexSrvGpuHandle);
    commandList->ExecuteIndirect(resources_->drawCommandSignature.Get(), 1,
                                 resources_->drawArgsResource.Get(), 0, nullptr, 0);
}

void GPUParticleSystem::Draw(const Camera& camera) {
    if (!HasDrawResources() || ShouldSkipDraw()) {
        return;
    }

    ID3D12GraphicsCommandList* cmd = nullptr;
    ConstantFrame* constantFrame = PrepareDrawFrame(cmd);
    if (constantFrame == nullptr) {
        return;
    }
    if (updatePending_ && dxCommon_->IsCommandListRecording()) {
        DispatchUpdate();
    }

    UpdateDrawConstants(camera, *constantFrame);
    D3D12_GPU_DESCRIPTOR_HANDLE baseTextureHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE noiseTextureHandle{};
    if (!ResolveDrawTextureHandles(baseTextureHandle, noiseTextureHandle)) {
        return;
    }
    RecordDrawCommands(cmd, *constantFrame, baseTextureHandle, noiseTextureHandle);
}

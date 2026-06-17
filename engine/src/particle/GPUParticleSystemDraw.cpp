#include "particle/GPUParticleSystem.h"
#include "GPUParticleEmitterUtils.h"
#include "GPUParticleSystemInternal.h"
#include "GPUParticleSystemShared.h"
#include "graphics/DirectXCommon.h"
#include "graphics/SrvManager.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
using GpuParticleEmitterUtils::IsContinuousEmitter;
using GpuParticleEmitterUtils::ResolveTextureId;
}

void GPUParticleSystem::Draw(const Camera &camera) {
    if (!dxCommon_ || !srvManager_ || !textureManager_ ||
        !dxCommon_->IsCommandListRecording() ||
        !resources_->particleResource || !resources_->activeIndexResource || !resources_->drawArgsResource ||
        !resources_->drawCommandSignature || !resources_->drawRootSignature || !resources_->drawPso ||
        !HasConstantBuffers() ||
        resources_->particleSrvGpuHandle.ptr == 0 || resources_->activeIndexSrvGpuHandle.ptr == 0) {
        return;
    }
    if (!updatePending_ && pendingEmitSettings_.empty() &&
        activeTimeRemaining_ <= 0.0f &&
        !IsContinuousEmitter(emitterSettings_)) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    if (cmd == nullptr || heap == nullptr) {
        return;
    }
    ConstantFrame *constantFrame = GetCurrentConstantFrame();
    if (constantFrame == nullptr ||
        !constantFrame->drawConstantBuffer ||
        constantFrame->mappedDrawCB == nullptr) {
        return;
    }
    ID3D12DescriptorHeap *heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);

    if (updatePending_ && dxCommon_->IsCommandListRecording()) {
        DispatchUpdate();
    }

    XMMATRIX viewProjection = camera.GetView() * camera.GetProj();
    XMStoreFloat4x4(&constantFrame->mappedDrawCB->viewProjection,
                    XMMatrixTranspose(viewProjection));

    XMMATRIX billboard = camera.GetView();
    billboard.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    const XMVECTOR billboardDeterminant = XMMatrixDeterminant(billboard);
    const float billboardDeterminantValue =
        XMVectorGetX(billboardDeterminant);
    billboard = std::isfinite(billboardDeterminantValue) &&
                        std::abs(billboardDeterminantValue) > 0.000001f
                    ? XMMatrixInverse(nullptr, billboard)
                    : XMMatrixIdentity();

    XMFLOAT3 right{};
    XMFLOAT3 up{};
    XMStoreFloat3(&right, billboard.r[0]);
    XMStoreFloat3(&up, billboard.r[1]);
    constantFrame->mappedDrawCB->cameraRight = {right.x, right.y, right.z, 0.0f};
    constantFrame->mappedDrawCB->cameraUp = {up.x, up.y, up.z, 0.0f};
    constantFrame->mappedDrawCB->tintColor = {1.0f, 1.0f, 1.0f, 1.0f};
    constantFrame->mappedDrawCB->atlasInfo = {
        static_cast<float>((std::max)(1u, emitterSettings_.atlasColumns)),
        static_cast<float>((std::max)(1u, emitterSettings_.atlasRows)),
        0.0f,
        0.0f};
    constantFrame->mappedDrawCB->materialParams0 = materialSettings_.params0;
    constantFrame->mappedDrawCB->materialParams1 = materialSettings_.params1;

    const uint32_t whiteTextureId = textureManager_->GetWhiteTextureId();
    const uint32_t noiseTextureId = ResolveTextureId(
        textureManager_, materialSettings_.noiseTextureId, whiteTextureId);
    const uint32_t baseTextureId =
        ResolveTextureId(textureManager_, textureId_, whiteTextureId);
    const D3D12_GPU_DESCRIPTOR_HANDLE baseTextureHandle =
        textureManager_->GetGpuHandle(baseTextureId);
    const D3D12_GPU_DESCRIPTOR_HANDLE noiseTextureHandle =
        textureManager_->GetGpuHandle(noiseTextureId);
    if (baseTextureHandle.ptr == 0 || noiseTextureHandle.ptr == 0) {
        return;
    }

    cmd->SetGraphicsRootSignature(resources_->drawRootSignature.Get());
    cmd->SetPipelineState(resources_->drawPso.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootConstantBufferView(
        0, constantFrame->drawConstantBuffer->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, resources_->particleSrvGpuHandle);
    cmd->SetGraphicsRootDescriptorTable(2, baseTextureHandle);
    cmd->SetGraphicsRootDescriptorTable(3, noiseTextureHandle);
    cmd->SetGraphicsRootDescriptorTable(4, resources_->activeIndexSrvGpuHandle);
    cmd->ExecuteIndirect(resources_->drawCommandSignature.Get(), 1, resources_->drawArgsResource.Get(),
                         0, nullptr, 0);
}

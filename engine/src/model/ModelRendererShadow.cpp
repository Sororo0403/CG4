#include "graphics/DirectXCommon.h"
#include "graphics/SrvManager.h"
#include "internal/ModelRendererInternal.h"
#include "model/MaterialManager.h"
#include "model/MeshManager.h"
#include "model/ModelRenderer.h"
#include "model/RendererMath.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"

using namespace DirectX;

void ModelRenderer::PreDrawShadow() {
    if (!state_->dxCommon || !state_->srvManager || !state_->shadowRootSignature ||
        !state_->shadowPSO) {
        state_->currentGraphicsRootSignature = nullptr;
        state_->currentGraphicsPipelineState = nullptr;
        return;
    }
    auto cmd = state_->dxCommon->GetCommandList();
    ID3D12DescriptorHeap* heap = state_->srvManager->GetHeap();
    if (cmd == nullptr || heap == nullptr) {
        state_->currentGraphicsRootSignature = nullptr;
        state_->currentGraphicsPipelineState = nullptr;
        return;
    }
    ID3D12DescriptorHeap* heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(state_->shadowRootSignature.Get());
    cmd->SetPipelineState(state_->shadowPSO.Get());
    state_->currentGraphicsRootSignature = state_->shadowRootSignature.Get();
    state_->currentGraphicsPipelineState = state_->shadowPSO.Get();
}

void ModelRenderer::DrawShadow(const Model& model, const Transform& transform,
                               const DirectX::XMFLOAT4X4& lightViewProjection) {
    if (!state_->dxCommon || !state_->meshManager || !state_->textureManager ||
        !state_->materialManager || !state_->shadowRootSignature || !state_->shadowPSO ||
        state_->drawIndex >= kMaxDraws) {
        return;
    }

    if (state_->dxCommon->GetCommandList() == nullptr) {
        return;
    }
    XMMATRIX world = RendererMath::MakeWorldMatrix(transform);

    if (model.hasRootAnimation) {
        world = XMLoadFloat4x4(&model.rootAnimationMatrix) * world;
    }

    const XMMATRIX lightVP = XMLoadFloat4x4(&lightViewProjection);
    const XMMATRIX wvp = world * lightVP;
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(wvp, world, XMMatrixIdentity());
    if (objectCbAddr == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    for (const auto& subMesh : model.subMeshes) {
        SubmitShadowSubMeshDraw(subMesh, objectCbAddr, state_->shadowPSO.Get(), nullptr, 1);
        if (state_->drawIndex >= kMaxDraws) {
            break;
        }
    }
}

void ModelRenderer::DrawInstancedShadow(const Model& model, const Transform* transforms,
                                        uint32_t instanceCount,
                                        const DirectX::XMFLOAT4X4& lightViewProjection) {
    if (!state_->dxCommon || !state_->meshManager || !state_->textureManager ||
        !state_->materialManager || !state_->shadowRootSignature || !state_->instancedShadowPSO ||
        !transforms || instanceCount == 0 || state_->drawIndex >= kMaxDraws) {
        return;
    }

    if (state_->dxCommon->GetCommandList() == nullptr) {
        return;
    }
    const XMMATRIX lightVP = XMLoadFloat4x4(&lightViewProjection);
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(lightVP, XMMatrixIdentity(), XMMatrixIdentity());
    const D3D12_VERTEX_BUFFER_VIEW instanceView = WriteInstances(model, transforms, instanceCount);
    if (objectCbAddr == 0 || instanceView.BufferLocation == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    for (const auto& subMesh : model.subMeshes) {
        SubmitShadowSubMeshDraw(subMesh, objectCbAddr, state_->instancedShadowPSO.Get(),
                                &instanceView, instanceCount);
        if (state_->drawIndex >= kMaxDraws) {
            break;
        }
    }
}

void ModelRenderer::DrawInstancedShadow(const Model& model, const InstanceData* instances,
                                        uint32_t instanceCount,
                                        const DirectX::XMFLOAT4X4& lightViewProjection) {
    if (!state_->dxCommon || !state_->meshManager || !state_->textureManager ||
        !state_->materialManager || !state_->shadowRootSignature || !state_->instancedShadowPSO ||
        !instances || instanceCount == 0 || state_->drawIndex >= kMaxDraws) {
        return;
    }

    if (state_->dxCommon->GetCommandList() == nullptr) {
        return;
    }
    const XMMATRIX lightVP = XMLoadFloat4x4(&lightViewProjection);
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(lightVP, XMMatrixIdentity(), XMMatrixIdentity());
    const D3D12_VERTEX_BUFFER_VIEW instanceView = WriteInstances(model, instances, instanceCount);
    if (objectCbAddr == 0 || instanceView.BufferLocation == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    for (const auto& subMesh : model.subMeshes) {
        SubmitShadowSubMeshDraw(subMesh, objectCbAddr, state_->instancedShadowPSO.Get(),
                                &instanceView, instanceCount);
        if (state_->drawIndex >= kMaxDraws) {
            break;
        }
    }
}

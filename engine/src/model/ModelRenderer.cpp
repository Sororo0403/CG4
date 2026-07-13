#include "model/ModelRenderer.h"

#include "graphics/DirectXCommon.h"
#include "graphics/SrvManager.h"
#include "internal/ModelRendererInternal.h"
#include "internal/RendererShadowMapUtils.h"
#include "model/RendererMath.h"
#include "texture/TextureManager.h"

using namespace DirectX;

void ModelRenderer::PreDraw() {
    if (!state_->dxCommon || !state_->srvManager || !state_->rootSignature) {
        state_->currentGraphicsRootSignature = nullptr;
        state_->currentGraphicsPipelineState = nullptr;
        state_->drawIndex = 0;
        return;
    }
    auto cmd = state_->dxCommon->GetCommandList();
    ID3D12DescriptorHeap* heap = state_->srvManager->GetHeap();
    if (cmd == nullptr || heap == nullptr) {
        state_->currentGraphicsRootSignature = nullptr;
        state_->currentGraphicsPipelineState = nullptr;
        state_->drawIndex = 0;
        return;
    }

    ID3D12DescriptorHeap* heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);

    cmd->SetGraphicsRootSignature(state_->rootSignature.Get());
    state_->currentGraphicsRootSignature = state_->rootSignature.Get();
    state_->currentGraphicsPipelineState = nullptr;
    state_->commandCache->Reset();
    state_->commandCache->rootSignature = state_->rootSignature.Get();

    state_->drawIndex = 0;
}

void ModelRenderer::Draw(const Model& model, const Transform& transform, const Camera& camera,
                         uint32_t environmentTextureId) {
    if (!state_->dxCommon || !state_->meshManager || !state_->textureManager ||
        !state_->materialManager || !state_->rootSignature || state_->drawIndex >= kMaxDraws) {
        return;
    }

    if (state_->dxCommon->GetCommandList() == nullptr) {
        return;
    }

    XMMATRIX world = RendererMath::MakeWorldMatrix(transform);

    if (model.hasRootAnimation) {
        world = XMLoadFloat4x4(&model.rootAnimationMatrix) * world;
    }

    XMMATRIX worldInverseTranspose = RendererMath::MakeSafeInverseTranspose(world);

    XMMATRIX wvp = world * camera.GetView() * camera.GetProj();
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(wvp, world, worldInverseTranspose);
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS effectCbAddr = WriteDrawEffectConstants();
    if (objectCbAddr == 0 || sceneCbAddr == 0 || effectCbAddr == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    const D3D12_GPU_VIRTUAL_ADDRESS identityPaletteAddress = GetIdentityPaletteAddress();

    if (!model.subMeshes.empty()) {
        for (const auto& subMesh : model.subMeshes) {
            SubmitForwardSubMeshDraw({&subMesh, objectCbAddr, sceneCbAddr, effectCbAddr,
                                      environmentTextureId, identityPaletteAddress, nullptr, 1,
                                      false});
            if (state_->drawIndex >= kMaxDraws) {
                break;
            }
        }
    }
}

void ModelRenderer::DrawInstanced(const Model& model, const Transform* transforms,
                                  uint32_t instanceCount, const Camera& camera,
                                  uint32_t environmentTextureId) {
    if (!state_->dxCommon || !state_->meshManager || !state_->textureManager ||
        !state_->materialManager || !state_->rootSignature || !transforms || instanceCount == 0) {
        return;
    }

    if (state_->dxCommon->GetCommandList() == nullptr) {
        return;
    }

    const D3D12_VERTEX_BUFFER_VIEW instanceView = WriteInstances(model, transforms, instanceCount);
    DrawInstancedWithPreparedBuffer(model, instanceView, instanceCount, camera,
                                    environmentTextureId);
}

void ModelRenderer::DrawInstanced(const Model& model, const InstanceData* instances,
                                  uint32_t instanceCount, const Camera& camera,
                                  uint32_t environmentTextureId) {
    if (!state_->dxCommon || !state_->meshManager || !state_->textureManager ||
        !state_->materialManager || !state_->rootSignature || !instances || instanceCount == 0) {
        return;
    }

    if (state_->dxCommon->GetCommandList() == nullptr) {
        return;
    }

    const D3D12_VERTEX_BUFFER_VIEW instanceView = WriteInstances(model, instances, instanceCount);
    DrawInstancedWithPreparedBuffer(model, instanceView, instanceCount, camera,
                                    environmentTextureId);
}

void ModelRenderer::DrawInstancedWithPreparedBuffer(const Model& model,
                                                    const D3D12_VERTEX_BUFFER_VIEW& instanceView,
                                                    uint32_t instanceCount, const Camera& camera,
                                                    uint32_t environmentTextureId) {
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(), XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS effectCbAddr = WriteDrawEffectConstants();
    if (objectCbAddr == 0 || sceneCbAddr == 0 || effectCbAddr == 0 ||
        instanceView.BufferLocation == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    const D3D12_GPU_VIRTUAL_ADDRESS identityPaletteAddress = GetIdentityPaletteAddress();

    for (const auto& subMesh : model.subMeshes) {
        SubmitForwardSubMeshDraw({&subMesh, objectCbAddr, sceneCbAddr, effectCbAddr,
                                  environmentTextureId, identityPaletteAddress, &instanceView,
                                  instanceCount, true});
        if (state_->drawIndex >= kMaxDraws) {
            break;
        }
    }
}

void ModelRenderer::PostDraw() {}

void ModelRenderer::SetShadowMap(D3D12_GPU_DESCRIPTOR_HANDLE shadowMap,
                                 const DirectX::XMFLOAT4X4& lightViewProjection,
                                 const SceneShadowSettings& settings) {
    RendererShadowMapUtils::Set(state_->textureManager, shadowMap, lightViewProjection, settings,
                                state_->shadowMapGpuHandle, state_->shadowLightViewProjection,
                                state_->shadowParams, state_->shadowFilterParams);
}

void ModelRenderer::SetSpotLightShadowMap(D3D12_GPU_DESCRIPTOR_HANDLE shadowMap,
                                          const DirectX::XMFLOAT4X4& lightViewProjection,
                                          const SceneShadowSettings& settings) {
    RendererShadowMapUtils::Set(state_->textureManager, shadowMap, lightViewProjection, settings,
                                state_->spotLightShadowMapGpuHandle,
                                state_->spotLightViewProjection, state_->spotShadowParams,
                                state_->spotShadowFilterParams);
}

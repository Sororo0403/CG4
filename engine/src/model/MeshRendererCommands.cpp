#include "graphics/DirectXCommon.h"
#include "internal/MeshRendererInternal.h"
#include "internal/RendererMaterialUtils.h"
#include "model/MeshRenderer.h"
#include "texture/TextureManager.h"

using RendererMaterialUtils::PipelineVariantIndex;
using RendererMaterialUtils::ResolveBaseColorTextureId;
using RendererMaterialUtils::ResolveCubeTextureId;
using RendererMaterialUtils::ResolveMetallicTextureId;
using RendererMaterialUtils::ResolveNormalTextureId;
using RendererMaterialUtils::ResolveRoughnessTextureId;

namespace {

D3D12_GPU_DESCRIPTOR_HANDLE GetTextureHandleOrFallback(const TextureManager* textureManager,
                                                       uint32_t textureId,
                                                       uint32_t fallbackTextureId) {
    if (textureManager == nullptr) {
        return {};
    }

    D3D12_GPU_DESCRIPTOR_HANDLE handle = textureManager->GetGpuHandle(textureId);
    if (handle.ptr != 0) {
        return handle;
    }
    return textureManager->GetGpuHandle(fallbackTextureId);
}

D3D12_GPU_DESCRIPTOR_HANDLE GetCubeTextureHandleOrFallback(const TextureManager* textureManager,
                                                           uint32_t textureId,
                                                           uint32_t fallbackTextureId) {
    if (textureManager == nullptr) {
        return {};
    }
    if (textureManager->IsCubeTextureId(textureId)) {
        const D3D12_GPU_DESCRIPTOR_HANDLE handle = textureManager->GetGpuHandle(textureId);
        if (handle.ptr != 0) {
            return handle;
        }
    }
    if (textureManager->IsCubeTextureId(fallbackTextureId)) {
        return textureManager->GetGpuHandle(fallbackTextureId);
    }
    return {};
}

D3D12_GPU_DESCRIPTOR_HANDLE GetWhiteTextureHandle(const TextureManager* textureManager) {
    if (textureManager == nullptr) {
        return {};
    }
    return textureManager->GetGpuHandle(textureManager->GetWhiteTextureId());
}

D3D12_GPU_DESCRIPTOR_HANDLE GetDefaultNormalTextureHandle(const TextureManager* textureManager) {
    if (textureManager == nullptr) {
        return {};
    }
    return textureManager->GetGpuHandle(textureManager->GetDefaultNormalTextureId());
}

D3D12_GPU_DESCRIPTOR_HANDLE ResolveShadowHandle(const TextureManager* textureManager,
                                                D3D12_GPU_DESCRIPTOR_HANDLE handle) {
    return handle.ptr != 0 ? handle : GetWhiteTextureHandle(textureManager);
}

} // namespace

void MeshRenderer::SetGraphicsRootSignatureCached(ID3D12RootSignature* rootSignature) {
    auto* cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr || rootSignature == nullptr) {
        return;
    }
    if (state_->commandCache->rootSignature == rootSignature) {
        return;
    }
    cmd->SetGraphicsRootSignature(rootSignature);
    state_->commandCache->rootSignature = rootSignature;
    state_->commandCache->rootParameterKinds.fill(
        MeshRendererCommandCache::RootParameterKind::None);
    state_->commandCache->rootParameterValues.fill(0);
}

void MeshRenderer::SetPipelineStateCached(ID3D12PipelineState* pipelineState) {
    auto* cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr || pipelineState == nullptr) {
        return;
    }
    if (state_->commandCache->pipelineState == pipelineState) {
        return;
    }
    cmd->SetPipelineState(pipelineState);
    state_->commandCache->pipelineState = pipelineState;
}

void MeshRenderer::SetGraphicsRootConstantBufferViewCached(uint32_t rootIndex,
                                                           D3D12_GPU_VIRTUAL_ADDRESS address) {
    auto* cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr || rootIndex >= state_->commandCache->rootParameterValues.size()) {
        return;
    }
    if (state_->commandCache->rootParameterKinds[rootIndex] ==
            MeshRendererCommandCache::RootParameterKind::ConstantBuffer &&
        state_->commandCache->rootParameterValues[rootIndex] == address) {
        return;
    }
    cmd->SetGraphicsRootConstantBufferView(rootIndex, address);
    state_->commandCache->rootParameterKinds[rootIndex] =
        MeshRendererCommandCache::RootParameterKind::ConstantBuffer;
    state_->commandCache->rootParameterValues[rootIndex] = address;
}

void MeshRenderer::SetGraphicsRootDescriptorTableCached(uint32_t rootIndex,
                                                        D3D12_GPU_DESCRIPTOR_HANDLE handle) {
    auto* cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr || rootIndex >= state_->commandCache->rootParameterValues.size()) {
        return;
    }
    if (state_->commandCache->rootParameterKinds[rootIndex] ==
            MeshRendererCommandCache::RootParameterKind::DescriptorTable &&
        state_->commandCache->rootParameterValues[rootIndex] == handle.ptr) {
        return;
    }
    cmd->SetGraphicsRootDescriptorTable(rootIndex, handle);
    state_->commandCache->rootParameterKinds[rootIndex] =
        MeshRendererCommandCache::RootParameterKind::DescriptorTable;
    state_->commandCache->rootParameterValues[rootIndex] = handle.ptr;
}

void MeshRenderer::IASetVertexBuffersCached(uint32_t startSlot, uint32_t viewCount,
                                            const D3D12_VERTEX_BUFFER_VIEW* views) {
    auto* cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr || views == nullptr || viewCount == 0u ||
        viewCount > state_->commandCache->vertexBufferViews.size()) {
        return;
    }

    bool cached = state_->commandCache->vertexBuffersValid &&
                  state_->commandCache->vertexBufferStartSlot == startSlot &&
                  state_->commandCache->vertexBufferViewCount == viewCount;
    for (uint32_t index = 0u; cached && index < viewCount; ++index) {
        cached = MeshRendererCommandCache::SameVertexBufferView(
            state_->commandCache->vertexBufferViews[index], views[index]);
    }
    if (cached) {
        return;
    }

    cmd->IASetVertexBuffers(startSlot, viewCount, views);
    state_->commandCache->vertexBufferStartSlot = startSlot;
    state_->commandCache->vertexBufferViewCount = viewCount;
    state_->commandCache->vertexBuffersValid = true;
    for (uint32_t index = 0u; index < viewCount; ++index) {
        state_->commandCache->vertexBufferViews[index] = views[index];
    }
}

void MeshRenderer::IASetIndexBufferCached(const D3D12_INDEX_BUFFER_VIEW& view) {
    auto* cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr) {
        return;
    }
    if (state_->commandCache->indexBufferValid &&
        MeshRendererCommandCache::SameIndexBufferView(state_->commandCache->indexBufferView,
                                                      view)) {
        return;
    }
    cmd->IASetIndexBuffer(&view);
    state_->commandCache->indexBufferView = view;
    state_->commandCache->indexBufferValid = true;
}

void MeshRenderer::IASetPrimitiveTopologyCached(D3D12_PRIMITIVE_TOPOLOGY topology) {
    auto* cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr) {
        return;
    }
    if (state_->commandCache->primitiveTopology == topology) {
        return;
    }
    cmd->IASetPrimitiveTopology(topology);
    state_->commandCache->primitiveTopology = topology;
}

void MeshRenderer::BindForwardMaterialDescriptors(const Material& drawMaterial, uint32_t textureId,
                                                  uint32_t normalTextureId) {
    const uint32_t environmentTextureId =
        ResolveCubeTextureId(state_->textureManager, state_->environmentTextureId,
                             state_->textureManager->GetBlackCubeTextureId());
    SetGraphicsRootDescriptorTableCached(
        3, GetTextureHandleOrFallback(
               state_->textureManager,
               ResolveBaseColorTextureId(state_->textureManager, drawMaterial, textureId),
               state_->textureManager->GetWhiteTextureId()));
    SetGraphicsRootDescriptorTableCached(
        4, GetCubeTextureHandleOrFallback(state_->textureManager, environmentTextureId,
                                          state_->textureManager->GetBlackCubeTextureId()));
    SetGraphicsRootDescriptorTableCached(
        5, ResolveShadowHandle(state_->textureManager, state_->shadowMapGpuHandle));
    SetGraphicsRootDescriptorTableCached(
        6, GetTextureHandleOrFallback(
               state_->textureManager,
               ResolveNormalTextureId(state_->textureManager, drawMaterial, normalTextureId),
               state_->textureManager->GetDefaultNormalTextureId()));
    SetGraphicsRootDescriptorTableCached(
        7, ResolveShadowHandle(state_->textureManager, state_->spotLightShadowMapGpuHandle));
    SetGraphicsRootDescriptorTableCached(
        8,
        GetTextureHandleOrFallback(state_->textureManager,
                                   ResolveRoughnessTextureId(state_->textureManager, drawMaterial),
                                   state_->textureManager->GetWhiteTextureId()));
    SetGraphicsRootDescriptorTableCached(
        9,
        GetTextureHandleOrFallback(state_->textureManager,
                                   ResolveMetallicTextureId(state_->textureManager, drawMaterial),
                                   state_->textureManager->GetWhiteTextureId()));
    SetGraphicsRootDescriptorTableCached(
        10, state_->textureManager->GetGpuHandle(state_->textureManager->GetWhiteTextureId()));
}

void MeshRenderer::BindForwardMaterialDescriptorHandles(
    D3D12_GPU_DESCRIPTOR_HANDLE textureHandle, D3D12_GPU_DESCRIPTOR_HANDLE normalTextureHandle) {
    const D3D12_GPU_DESCRIPTOR_HANDLE baseColorHandle =
        textureHandle.ptr != 0
            ? textureHandle
            : state_->textureManager->GetGpuHandle(state_->textureManager->GetWhiteTextureId());
    const D3D12_GPU_DESCRIPTOR_HANDLE normalHandle =
        normalTextureHandle.ptr != 0 ? normalTextureHandle
                                     : GetDefaultNormalTextureHandle(state_->textureManager);
    const uint32_t environmentTextureId =
        ResolveCubeTextureId(state_->textureManager, state_->environmentTextureId,
                             state_->textureManager->GetBlackCubeTextureId());
    SetGraphicsRootDescriptorTableCached(3, baseColorHandle);
    SetGraphicsRootDescriptorTableCached(
        4, GetCubeTextureHandleOrFallback(state_->textureManager, environmentTextureId,
                                          state_->textureManager->GetBlackCubeTextureId()));
    SetGraphicsRootDescriptorTableCached(
        5, ResolveShadowHandle(state_->textureManager, state_->shadowMapGpuHandle));
    SetGraphicsRootDescriptorTableCached(6, normalHandle);
    SetGraphicsRootDescriptorTableCached(
        7, ResolveShadowHandle(state_->textureManager, state_->spotLightShadowMapGpuHandle));
    SetGraphicsRootDescriptorTableCached(
        8, state_->textureManager->GetGpuHandle(state_->textureManager->GetWhiteTextureId()));
    SetGraphicsRootDescriptorTableCached(
        9, state_->textureManager->GetGpuHandle(state_->textureManager->GetWhiteTextureId()));
    SetGraphicsRootDescriptorTableCached(
        10, state_->textureManager->GetGpuHandle(state_->textureManager->GetWhiteTextureId()));
}

void MeshRenderer::BindShadowMaterialDescriptor(const Material& drawMaterial, uint32_t textureId) {
    SetGraphicsRootDescriptorTableCached(
        3, state_->textureManager->GetGpuHandle(
               ResolveBaseColorTextureId(state_->textureManager, drawMaterial, textureId)));
}

void MeshRenderer::SubmitForwardMeshDraw(const Mesh& mesh, const Material& drawMaterial,
                                         const MeshDrawConstants& constants,
                                         const MeshVertexViewSpan& vertices,
                                         const ForwardTextureIds& textures) {
    auto* cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr || vertices.views == nullptr || vertices.count == 0 ||
        vertices.instanceCount == 0) {
        return;
    }

    SetGraphicsRootConstantBufferViewCached(0, constants.object);
    SetGraphicsRootConstantBufferViewCached(1, constants.scene);
    SetGraphicsRootConstantBufferViewCached(2, constants.material);
    BindForwardMaterialDescriptors(drawMaterial, textures.baseColor, textures.normal);
    IASetVertexBuffersCached(0, vertices.count, vertices.views);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, vertices.instanceCount, 0, 0, 0);
    ++state_->drawIndex;
}

void MeshRenderer::SubmitForwardMeshDrawWithHandles(const Mesh& mesh,
                                                    const MeshDrawConstants& constants,
                                                    const MeshVertexViewSpan& vertices,
                                                    const ForwardTextureHandles& textures) {
    auto* cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr || vertices.views == nullptr || vertices.count == 0 ||
        vertices.instanceCount == 0) {
        return;
    }

    SetGraphicsRootConstantBufferViewCached(0, constants.object);
    SetGraphicsRootConstantBufferViewCached(1, constants.scene);
    SetGraphicsRootConstantBufferViewCached(2, constants.material);
    BindForwardMaterialDescriptorHandles(textures.baseColor, textures.normal);
    IASetVertexBuffersCached(0, vertices.count, vertices.views);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, vertices.instanceCount, 0, 0, 0);
    ++state_->drawIndex;
}

void MeshRenderer::SubmitShadowMeshDraw(const Mesh& mesh, const Material& drawMaterial,
                                        const MeshDrawConstants& constants,
                                        const MeshVertexViewSpan& vertices, uint32_t textureId) {
    auto* cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr || vertices.views == nullptr || vertices.count == 0 ||
        vertices.instanceCount == 0) {
        return;
    }

    SetGraphicsRootConstantBufferViewCached(0, constants.object);
    SetGraphicsRootConstantBufferViewCached(1, constants.scene);
    SetGraphicsRootConstantBufferViewCached(2, constants.material);
    BindShadowMaterialDescriptor(drawMaterial, textureId);
    IASetVertexBuffersCached(0, vertices.count, vertices.views);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, vertices.instanceCount, 0, 0, 0);
    ++state_->drawIndex;
}

bool MeshRenderer::SetPipelineForMaterial(const Material& material) {
    return SetPipelineStateForMaterial(state_->pipelineStates, material);
}

bool MeshRenderer::SetPipelineForMaterial(const PipelineStateArray& pipelineStates,
                                          const Material& material) {
    return SetPipelineStateForMaterial(pipelineStates, material);
}

bool MeshRenderer::SetInstancedPipelineForMaterial(const Material& material) {
    return SetPipelineStateForMaterial(state_->instancedPipelineStates, material);
}

bool MeshRenderer::SetInstancedPipelineForMaterial(const PipelineStateArray& pipelineStates,
                                                   const Material& material) {
    return SetPipelineStateForMaterial(pipelineStates, material);
}

bool MeshRenderer::SetInstancedShadowPipelineForMaterial(const PipelineStateArray& pipelineStates,
                                                         const Material& material) {
    return SetPipelineStateForMaterial(pipelineStates, material);
}

bool MeshRenderer::SetPipelineStateForMaterial(const PipelineStateArray& pipelineStates,
                                               const Material& material) {
    auto* cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    ID3D12PipelineState* pipelineState = pipelineStates[PipelineVariantIndex(material)].Get();
    if (cmd == nullptr || pipelineState == nullptr) {
        return false;
    }
    SetPipelineStateCached(pipelineState);
    return true;
}

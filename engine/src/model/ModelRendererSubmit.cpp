#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "internal/ModelRendererInternal.h"
#include "internal/RendererMaterialUtils.h"
#include "model/MaterialManager.h"
#include "model/MeshManager.h"
#include "model/ModelRenderer.h"
#include "texture/TextureManager.h"

#include <array>

using RendererMaterialUtils::ResolveBaseColorTextureId;
using RendererMaterialUtils::ResolveMetallicTextureId;
using RendererMaterialUtils::ResolveNormalTextureId;
using RendererMaterialUtils::ResolveRoughnessTextureId;

namespace {

uint32_t ResolveEnvironmentTextureId(const TextureManager* textureManager,
                                     uint32_t requestedTextureId, uint32_t configuredTextureId,
                                     bool hasConfiguredTexture) {
    if (textureManager == nullptr) {
        return kInvalidResourceId;
    }

    const uint32_t selectedTextureId =
        IsValidResourceId(requestedTextureId)
            ? requestedTextureId
            : (hasConfiguredTexture ? configuredTextureId : kInvalidResourceId);
    if (IsValidResourceId(selectedTextureId) &&
        textureManager->IsCubeTextureId(selectedTextureId)) {
        return selectedTextureId;
    }

    const uint32_t fallbackTextureId = textureManager->GetBlackCubeTextureId();
    return textureManager->IsCubeTextureId(fallbackTextureId) ? fallbackTextureId
                                                              : kInvalidResourceId;
}

bool IsDrawableSubMesh(const ModelSubMesh& subMesh, const MeshManager* meshManager,
                       const MaterialManager* materialManager) {
    return meshManager != nullptr && materialManager != nullptr &&
           meshManager->IsValidMeshId(subMesh.meshId) &&
           materialManager->IsValidMaterialId(subMesh.materialId);
}

bool HasCompleteSkinningDescriptors(const SkinCluster& skinCluster) {
    return skinCluster.inputVertexSrvGpuHandle.ptr != 0 &&
           skinCluster.influenceSrvGpuHandle.ptr != 0 &&
           skinCluster.skinnedVertexUavGpuHandle.ptr != 0;
}

D3D12_GPU_VIRTUAL_ADDRESS GetCurrentPaletteAddress(const SkinCluster& skinCluster,
                                                   const DirectXCommon* dxCommon) {
    if (skinCluster.paletteFrames.empty()) {
        return 0;
    }
    const size_t frameIndex =
        dxCommon != nullptr ? dxCommon->GetBackBufferIndex() % skinCluster.paletteFrames.size() : 0;
    if (frameIndex >= skinCluster.paletteFrames.size()) {
        return 0;
    }
    const SkinPaletteFrame& frame = skinCluster.paletteFrames[frameIndex];
    return frame.resource ? frame.resource->GetGPUVirtualAddress() : 0;
}

bool HasRenderableVertexSource(const ModelSubMesh& subMesh) {
    const SkinCluster& skinCluster = subMesh.skinCluster;
    if (!skinCluster.skinnedVertexResource) {
        return true;
    }

    return skinCluster.skinningValid && HasCompleteSkinningDescriptors(skinCluster) &&
           skinCluster.skinnedVertexBufferView.BufferLocation != 0 &&
           skinCluster.skinnedVertexBufferView.SizeInBytes > 0 &&
           skinCluster.skinnedVertexBufferView.StrideInBytes > 0;
}

bool IsDrawableSubMeshWithValidVertexSource(const ModelSubMesh& subMesh,
                                            const MeshManager* meshManager,
                                            const MaterialManager* materialManager) {
    return IsDrawableSubMesh(subMesh, meshManager, materialManager) &&
           HasRenderableVertexSource(subMesh);
}

bool HasPaletteBuffer(const ModelSubMesh& subMesh, const DirectXCommon* dxCommon,
                      D3D12_GPU_VIRTUAL_ADDRESS identityPaletteAddress) {
    const SkinCluster& skinCluster = subMesh.skinCluster;
    if (skinCluster.paletteCount > 0) {
        return GetCurrentPaletteAddress(skinCluster, dxCommon) != 0;
    }
    return identityPaletteAddress != 0;
}

D3D12_GPU_VIRTUAL_ADDRESS GetPaletteAddressForDraw(
    const SkinCluster& skinCluster, const DirectXCommon* dxCommon,
    D3D12_GPU_VIRTUAL_ADDRESS identityPaletteAddress) {
    if (skinCluster.paletteCount > 0) {
        return GetCurrentPaletteAddress(skinCluster, dxCommon);
    }
    return identityPaletteAddress;
}

bool IsForwardDrawableSubMesh(const ModelSubMesh& subMesh, const MeshManager* meshManager,
                              const MaterialManager* materialManager, const DirectXCommon* dxCommon,
                              D3D12_GPU_VIRTUAL_ADDRESS identityPaletteAddress) {
    return IsDrawableSubMesh(subMesh, meshManager, materialManager) &&
           HasPaletteBuffer(subMesh, dxCommon, identityPaletteAddress) &&
           HasRenderableVertexSource(subMesh);
}

void SetRootConstantBufferViewCached(auto& state, uint32_t rootIndex,
                                     D3D12_GPU_VIRTUAL_ADDRESS address) {
    auto* cmd = state.dxCommon ? state.dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr || state.commandCache == nullptr ||
        rootIndex >= state.commandCache->rootParameterValues.size()) {
        return;
    }
    if (state.commandCache->rootParameterKinds[rootIndex] ==
            MeshRendererCommandCache::RootParameterKind::ConstantBuffer &&
        state.commandCache->rootParameterValues[rootIndex] == address) {
        return;
    }
    cmd->SetGraphicsRootConstantBufferView(rootIndex, address);
    state.commandCache->rootParameterKinds[rootIndex] =
        MeshRendererCommandCache::RootParameterKind::ConstantBuffer;
    state.commandCache->rootParameterValues[rootIndex] = address;
}

void SetRootDescriptorTableCached(auto& state, uint32_t rootIndex,
                                  D3D12_GPU_DESCRIPTOR_HANDLE handle) {
    auto* cmd = state.dxCommon ? state.dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr || state.commandCache == nullptr ||
        rootIndex >= state.commandCache->rootParameterValues.size()) {
        return;
    }
    if (state.commandCache->rootParameterKinds[rootIndex] ==
            MeshRendererCommandCache::RootParameterKind::DescriptorTable &&
        state.commandCache->rootParameterValues[rootIndex] == handle.ptr) {
        return;
    }
    cmd->SetGraphicsRootDescriptorTable(rootIndex, handle);
    state.commandCache->rootParameterKinds[rootIndex] =
        MeshRendererCommandCache::RootParameterKind::DescriptorTable;
    state.commandCache->rootParameterValues[rootIndex] = handle.ptr;
}

void SetRootShaderResourceViewCached(auto& state, uint32_t rootIndex,
                                     D3D12_GPU_VIRTUAL_ADDRESS address) {
    auto* cmd = state.dxCommon ? state.dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr || state.commandCache == nullptr ||
        rootIndex >= state.commandCache->rootParameterValues.size()) {
        return;
    }
    if (state.commandCache->rootParameterKinds[rootIndex] ==
            MeshRendererCommandCache::RootParameterKind::ShaderResource &&
        state.commandCache->rootParameterValues[rootIndex] == address) {
        return;
    }
    cmd->SetGraphicsRootShaderResourceView(rootIndex, address);
    state.commandCache->rootParameterKinds[rootIndex] =
        MeshRendererCommandCache::RootParameterKind::ShaderResource;
    state.commandCache->rootParameterValues[rootIndex] = address;
}

void SetVertexBuffersCached(auto& state, uint32_t startSlot, uint32_t viewCount,
                            const D3D12_VERTEX_BUFFER_VIEW* views) {
    auto* cmd = state.dxCommon ? state.dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr || state.commandCache == nullptr || views == nullptr || viewCount == 0u ||
        viewCount > state.commandCache->vertexBufferViews.size()) {
        return;
    }

    bool cached = state.commandCache->vertexBuffersValid &&
                  state.commandCache->vertexBufferStartSlot == startSlot &&
                  state.commandCache->vertexBufferViewCount == viewCount;
    for (uint32_t index = 0u; cached && index < viewCount; ++index) {
        cached = MeshRendererCommandCache::SameVertexBufferView(
            state.commandCache->vertexBufferViews[index], views[index]);
    }
    if (cached) {
        return;
    }

    cmd->IASetVertexBuffers(startSlot, viewCount, views);
    state.commandCache->vertexBufferStartSlot = startSlot;
    state.commandCache->vertexBufferViewCount = viewCount;
    state.commandCache->vertexBuffersValid = true;
    for (uint32_t index = 0u; index < viewCount; ++index) {
        state.commandCache->vertexBufferViews[index] = views[index];
    }
}

void SetIndexBufferCached(auto& state, const D3D12_INDEX_BUFFER_VIEW& view) {
    auto* cmd = state.dxCommon ? state.dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr || state.commandCache == nullptr) {
        return;
    }
    if (state.commandCache->indexBufferValid &&
        MeshRendererCommandCache::SameIndexBufferView(state.commandCache->indexBufferView, view)) {
        return;
    }
    cmd->IASetIndexBuffer(&view);
    state.commandCache->indexBufferView = view;
    state.commandCache->indexBufferValid = true;
}

void SetPrimitiveTopologyCached(auto& state, D3D12_PRIMITIVE_TOPOLOGY topology) {
    auto* cmd = state.dxCommon ? state.dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr || state.commandCache == nullptr ||
        state.commandCache->primitiveTopology == topology) {
        return;
    }
    cmd->IASetPrimitiveTopology(topology);
    state.commandCache->primitiveTopology = topology;
}

bool SetShadowPipelineCached(auto& state, ID3D12PipelineState* pipelineState) {
    auto* cmd = state.dxCommon ? state.dxCommon->GetCommandList() : nullptr;
    ID3D12RootSignature* rootSignature = state.shadowRootSignature.Get();
    if (cmd == nullptr || rootSignature == nullptr || pipelineState == nullptr ||
        state.commandCache == nullptr) {
        return false;
    }
    if (state.commandCache->rootSignature != rootSignature) {
        cmd->SetGraphicsRootSignature(rootSignature);
        state.commandCache->Reset();
        state.commandCache->rootSignature = rootSignature;
        state.currentGraphicsRootSignature = rootSignature;
        state.currentGraphicsPipelineState = nullptr;
    }
    if (state.commandCache->pipelineState != pipelineState) {
        cmd->SetPipelineState(pipelineState);
        state.commandCache->pipelineState = pipelineState;
        state.currentGraphicsPipelineState = pipelineState;
    }
    return true;
}

} // namespace

bool ModelRenderer::SubmitForwardSubMeshDraw(const ForwardSubMeshDrawRequest& request) {
    if (request.subMesh == nullptr || state_->drawIndex >= kMaxDraws ||
        request.instanceCount == 0 ||
        !IsForwardDrawableSubMesh(*request.subMesh, state_->meshManager, state_->materialManager,
                                  state_->dxCommon, request.identityPaletteAddress)) {
        return false;
    }
    const ModelSubMesh& subMesh = *request.subMesh;

    auto* cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr) {
        return false;
    }

    const Material& material = state_->materialManager->GetMaterial(subMesh.materialId);
    if (request.instanced) {
        if (!SetInstancedPipelineForMaterial(material)) {
            return false;
        }
    } else if (!SetPipelineForMaterial(material)) {
        return false;
    }

    const Mesh& mesh = state_->meshManager->GetMesh(subMesh.meshId);
    const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
        subMesh.skinCluster.skinnedVertexResource ? subMesh.skinCluster.skinnedVertexBufferView
                                                  : mesh.vbView;
    const D3D12_GPU_VIRTUAL_ADDRESS paletteAddress = GetPaletteAddressForDraw(
        subMesh.skinCluster, state_->dxCommon, request.identityPaletteAddress);
    if (paletteAddress == 0) {
        return false;
    }

    const uint32_t safeEnvironmentTextureId =
        ResolveEnvironmentTextureId(state_->textureManager, request.environmentTextureId,
                                    state_->environmentTextureId, state_->hasEnvironmentTexture);
    if (!IsValidResourceId(safeEnvironmentTextureId)) {
        return false;
    }

    std::array<D3D12_VERTEX_BUFFER_VIEW, 2> views = {vertexBufferView, {}};
    uint32_t vertexViewCount = 1;
    if (request.instanceView != nullptr) {
        views[1] = *request.instanceView;
        vertexViewCount = 2;
    }

    SetRootConstantBufferViewCached(*state_, 0, request.objectCbAddr);
    SetRootConstantBufferViewCached(*state_, 1, request.sceneCbAddr);
    SetRootConstantBufferViewCached(
        *state_, 2, state_->materialManager->GetGPUVirtualAddress(subMesh.materialId));
    SetRootDescriptorTableCached(*state_, 3,
                                 state_->textureManager->GetGpuHandle(ResolveBaseColorTextureId(
                                     state_->textureManager, material, subMesh.textureId)));
    SetRootShaderResourceViewCached(*state_, 4, paletteAddress);
    SetRootDescriptorTableCached(*state_, 5,
                                 state_->textureManager->GetGpuHandle(safeEnvironmentTextureId));
    SetRootDescriptorTableCached(*state_, 6, state_->shadowMapGpuHandle);
    SetRootDescriptorTableCached(*state_, 7,
                                 state_->textureManager->GetGpuHandle(ResolveNormalTextureId(
                                     state_->textureManager, material, subMesh.normalTextureId)));
    SetRootConstantBufferViewCached(*state_, 8, request.effectCbAddr);
    SetRootDescriptorTableCached(
        *state_, 9, state_->textureManager->GetGpuHandle(state_->dissolveNoiseTextureId));
    SetRootDescriptorTableCached(*state_, 10, state_->spotLightShadowMapGpuHandle);
    SetRootDescriptorTableCached(*state_, 11,
                                 state_->textureManager->GetGpuHandle(
                                     ResolveRoughnessTextureId(state_->textureManager, material)));
    SetRootDescriptorTableCached(*state_, 12,
                                 state_->textureManager->GetGpuHandle(
                                     ResolveMetallicTextureId(state_->textureManager, material)));

    SetVertexBuffersCached(*state_, 0, vertexViewCount, views.data());
    SetIndexBufferCached(*state_, mesh.ibView);
    SetPrimitiveTopologyCached(*state_, mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, request.instanceCount, 0, 0, 0);
    ++state_->drawIndex;
    return true;
}

bool ModelRenderer::SubmitShadowSubMeshDraw(const ModelSubMesh& subMesh,
                                            D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr,
                                            ID3D12PipelineState* pipelineState,
                                            const D3D12_VERTEX_BUFFER_VIEW* instanceView,
                                            uint32_t instanceCount) {
    if (state_->drawIndex >= kMaxDraws || instanceCount == 0 || pipelineState == nullptr ||
        !IsDrawableSubMeshWithValidVertexSource(subMesh, state_->meshManager,
                                                state_->materialManager)) {
        return false;
    }

    auto* cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr) {
        return false;
    }

    if (!SetShadowPipelineCached(*state_, pipelineState)) {
        return false;
    }

    const Mesh& mesh = state_->meshManager->GetMesh(subMesh.meshId);
    const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
        subMesh.skinCluster.skinnedVertexResource ? subMesh.skinCluster.skinnedVertexBufferView
                                                  : mesh.vbView;
    std::array<D3D12_VERTEX_BUFFER_VIEW, 2> views = {vertexBufferView, {}};
    uint32_t vertexViewCount = 1;
    if (instanceView != nullptr) {
        views[1] = *instanceView;
        vertexViewCount = 2;
    }

    const Material& material = state_->materialManager->GetMaterial(subMesh.materialId);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
        state_->materialManager->GetGPUVirtualAddress(subMesh.materialId);
    SetRootConstantBufferViewCached(*state_, 0, objectCbAddr);
    SetRootConstantBufferViewCached(*state_, 1, materialCbAddr);
    SetRootDescriptorTableCached(*state_, 2,
                                 state_->textureManager->GetGpuHandle(ResolveBaseColorTextureId(
                                     state_->textureManager, material, subMesh.textureId)));
    SetVertexBuffersCached(*state_, 0, vertexViewCount, views.data());
    SetIndexBufferCached(*state_, mesh.ibView);
    SetPrimitiveTopologyCached(*state_, mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);
    ++state_->drawIndex;
    return true;
}

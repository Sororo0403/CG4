#include "model/ModelRenderer.h"
#include "internal/ModelRendererInternal.h"

#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "model/MaterialManager.h"
#include "model/MeshManager.h"
#include "internal/RendererMaterialUtils.h"
#include "texture/TextureManager.h"

#include <array>

using RendererMaterialUtils::ResolveBaseColorTextureId;
using RendererMaterialUtils::ResolveMetallicTextureId;
using RendererMaterialUtils::ResolveNormalTextureId;
using RendererMaterialUtils::ResolveRoughnessTextureId;

namespace {

uint32_t ResolveEnvironmentTextureId(const TextureManager *textureManager,
                                     uint32_t requestedTextureId,
                                     uint32_t configuredTextureId,
                                     bool hasConfiguredTexture) {
    if (textureManager == nullptr) {
        return kInvalidResourceId;
    }

    const uint32_t selectedTextureId =
        IsValidResourceId(requestedTextureId)
            ? requestedTextureId
            : (hasConfiguredTexture ? configuredTextureId
                                    : kInvalidResourceId);
    if (IsValidResourceId(selectedTextureId) &&
        textureManager->IsCubeTextureId(selectedTextureId)) {
        return selectedTextureId;
    }

    const uint32_t fallbackTextureId = textureManager->GetBlackCubeTextureId();
    return textureManager->IsCubeTextureId(fallbackTextureId)
               ? fallbackTextureId
               : kInvalidResourceId;
}

bool IsDrawableSubMesh(const ModelSubMesh &subMesh,
                       const MeshManager *meshManager,
                       const MaterialManager *materialManager) {
    return meshManager != nullptr && materialManager != nullptr &&
           meshManager->IsValidMeshId(subMesh.meshId) &&
           materialManager->IsValidMaterialId(subMesh.materialId);
}

bool HasCompleteSkinningDescriptors(const SkinCluster &skinCluster) {
    return skinCluster.inputVertexSrvGpuHandle.ptr != 0 &&
           skinCluster.influenceSrvGpuHandle.ptr != 0 &&
           skinCluster.skinnedVertexUavGpuHandle.ptr != 0;
}

D3D12_GPU_VIRTUAL_ADDRESS GetCurrentPaletteAddress(
    const SkinCluster &skinCluster, const DirectXCommon *dxCommon) {
    if (skinCluster.paletteFrames.empty()) {
        return 0;
    }
    const size_t frameIndex =
        dxCommon != nullptr
            ? dxCommon->GetBackBufferIndex() % skinCluster.paletteFrames.size()
            : 0;
    if (frameIndex >= skinCluster.paletteFrames.size()) {
        return 0;
    }
    const SkinPaletteFrame &frame = skinCluster.paletteFrames[frameIndex];
    return frame.resource ? frame.resource->GetGPUVirtualAddress() : 0;
}

bool HasRenderableVertexSource(const ModelSubMesh &subMesh) {
    const SkinCluster &skinCluster = subMesh.skinCluster;
    if (!skinCluster.skinnedVertexResource) {
        return true;
    }

    return skinCluster.skinningValid &&
           HasCompleteSkinningDescriptors(skinCluster) &&
           skinCluster.skinnedVertexBufferView.BufferLocation != 0 &&
           skinCluster.skinnedVertexBufferView.SizeInBytes > 0 &&
           skinCluster.skinnedVertexBufferView.StrideInBytes > 0;
}

bool IsDrawableSubMeshWithValidVertexSource(
    const ModelSubMesh &subMesh, const MeshManager *meshManager,
    const MaterialManager *materialManager) {
    return IsDrawableSubMesh(subMesh, meshManager, materialManager) &&
           HasRenderableVertexSource(subMesh);
}

bool HasPaletteBuffer(const ModelSubMesh &subMesh, const DirectXCommon *dxCommon,
                      D3D12_GPU_VIRTUAL_ADDRESS identityPaletteAddress) {
    const SkinCluster &skinCluster = subMesh.skinCluster;
    if (skinCluster.paletteCount > 0) {
        return GetCurrentPaletteAddress(skinCluster, dxCommon) != 0;
    }
    return identityPaletteAddress != 0;
}

D3D12_GPU_VIRTUAL_ADDRESS GetPaletteAddressForDraw(
    const SkinCluster &skinCluster, const DirectXCommon *dxCommon,
    D3D12_GPU_VIRTUAL_ADDRESS identityPaletteAddress) {
    if (skinCluster.paletteCount > 0) {
        return GetCurrentPaletteAddress(skinCluster, dxCommon);
    }
    return identityPaletteAddress;
}

bool IsForwardDrawableSubMesh(
    const ModelSubMesh &subMesh, const MeshManager *meshManager,
    const MaterialManager *materialManager, const DirectXCommon *dxCommon,
    D3D12_GPU_VIRTUAL_ADDRESS identityPaletteAddress) {
    return IsDrawableSubMesh(subMesh, meshManager, materialManager) &&
           HasPaletteBuffer(subMesh, dxCommon, identityPaletteAddress) &&
           HasRenderableVertexSource(subMesh);
}

} // namespace

bool ModelRenderer::SubmitForwardSubMeshDraw(
    const ModelSubMesh &subMesh, D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr,
    D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr,
    D3D12_GPU_VIRTUAL_ADDRESS effectCbAddr, uint32_t environmentTextureId,
    D3D12_GPU_VIRTUAL_ADDRESS identityPaletteAddress,
    const D3D12_VERTEX_BUFFER_VIEW *instanceView, uint32_t instanceCount,
    bool instanced) {
    if (state_->drawIndex >= kMaxDraws || instanceCount == 0 ||
        !IsForwardDrawableSubMesh(subMesh, state_->meshManager,
                                  state_->materialManager, state_->dxCommon,
                                  identityPaletteAddress)) {
        return false;
    }

    auto *cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr) {
        return false;
    }

    const Material &material =
        state_->materialManager->GetMaterial(subMesh.materialId);
    if (instanced) {
        if (!SetInstancedPipelineForMaterial(material)) {
            return false;
        }
    } else if (!SetPipelineForMaterial(material)) {
        return false;
    }

    const Mesh &mesh = state_->meshManager->GetMesh(subMesh.meshId);
    const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
        subMesh.skinCluster.skinnedVertexResource
            ? subMesh.skinCluster.skinnedVertexBufferView
            : mesh.vbView;
    const D3D12_GPU_VIRTUAL_ADDRESS paletteAddress =
        GetPaletteAddressForDraw(subMesh.skinCluster, state_->dxCommon,
                                 identityPaletteAddress);
    if (paletteAddress == 0) {
        return false;
    }

    const uint32_t safeEnvironmentTextureId =
        ResolveEnvironmentTextureId(state_->textureManager, environmentTextureId,
                                    state_->environmentTextureId,
                                    state_->hasEnvironmentTexture);
    if (!IsValidResourceId(safeEnvironmentTextureId)) {
        return false;
    }

    std::array<D3D12_VERTEX_BUFFER_VIEW, 2> views = {vertexBufferView, {}};
    uint32_t vertexViewCount = 1;
    if (instanceView != nullptr) {
        views[1] = *instanceView;
        vertexViewCount = 2;
    }

    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
    cmd->SetGraphicsRootConstantBufferView(
        2, state_->materialManager->GetGPUVirtualAddress(subMesh.materialId));
    cmd->SetGraphicsRootDescriptorTable(
        3, state_->textureManager->GetGpuHandle(ResolveBaseColorTextureId(
               state_->textureManager, material, subMesh.textureId)));
    cmd->SetGraphicsRootShaderResourceView(4, paletteAddress);
    cmd->SetGraphicsRootDescriptorTable(
        5, state_->textureManager->GetGpuHandle(safeEnvironmentTextureId));
    cmd->SetGraphicsRootDescriptorTable(6, state_->shadowMapGpuHandle);
    cmd->SetGraphicsRootDescriptorTable(
        7, state_->textureManager->GetGpuHandle(ResolveNormalTextureId(
               state_->textureManager, material, subMesh.normalTextureId)));
    cmd->SetGraphicsRootConstantBufferView(8, effectCbAddr);
    cmd->SetGraphicsRootDescriptorTable(
        9, state_->textureManager->GetGpuHandle(state_->dissolveNoiseTextureId));
    cmd->SetGraphicsRootDescriptorTable(10,
                                        state_->spotLightShadowMapGpuHandle);
    cmd->SetGraphicsRootDescriptorTable(
        11, state_->textureManager->GetGpuHandle(
                ResolveRoughnessTextureId(state_->textureManager, material)));
    cmd->SetGraphicsRootDescriptorTable(
        12, state_->textureManager->GetGpuHandle(
                ResolveMetallicTextureId(state_->textureManager, material)));

    cmd->IASetVertexBuffers(0, vertexViewCount, views.data());
    cmd->IASetIndexBuffer(&mesh.ibView);
    cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);
    ++state_->drawIndex;
    return true;
}

bool ModelRenderer::SubmitShadowSubMeshDraw(
    const ModelSubMesh &subMesh, D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr,
    ID3D12PipelineState *pipelineState,
    const D3D12_VERTEX_BUFFER_VIEW *instanceView, uint32_t instanceCount) {
    if (state_->drawIndex >= kMaxDraws || instanceCount == 0 ||
        pipelineState == nullptr ||
        !IsDrawableSubMeshWithValidVertexSource(
            subMesh, state_->meshManager, state_->materialManager)) {
        return false;
    }

    auto *cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    if (cmd == nullptr) {
        return false;
    }

    cmd->SetGraphicsRootSignature(state_->shadowRootSignature.Get());
    cmd->SetPipelineState(pipelineState);

    const Mesh &mesh = state_->meshManager->GetMesh(subMesh.meshId);
    const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
        subMesh.skinCluster.skinnedVertexResource
            ? subMesh.skinCluster.skinnedVertexBufferView
            : mesh.vbView;
    std::array<D3D12_VERTEX_BUFFER_VIEW, 2> views = {vertexBufferView, {}};
    uint32_t vertexViewCount = 1;
    if (instanceView != nullptr) {
        views[1] = *instanceView;
        vertexViewCount = 2;
    }

    const Material &material =
        state_->materialManager->GetMaterial(subMesh.materialId);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
        state_->materialManager->GetGPUVirtualAddress(subMesh.materialId);
    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, materialCbAddr);
    cmd->SetGraphicsRootDescriptorTable(
        2, state_->textureManager->GetGpuHandle(ResolveBaseColorTextureId(
               state_->textureManager, material, subMesh.textureId)));
    cmd->IASetVertexBuffers(0, vertexViewCount, views.data());
    cmd->IASetIndexBuffer(&mesh.ibView);
    cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);
    ++state_->drawIndex;
    return true;
}

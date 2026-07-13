#include "graphics/DirectXCommon.h"
#include "internal/MeshRendererInternal.h"
#include "internal/RendererMaterialUtils.h"
#include "model/MeshRenderer.h"
#include "model/RendererMath.h"

using namespace DirectX;

namespace {
using RendererMaterialUtils::IsDrawableMesh;
} // namespace

void MeshRenderer::DrawMeshShadow(const Mesh& mesh, const Transform& transform,
                                  const DirectX::XMFLOAT4X4& lightViewProjection) {
    DrawMeshShadow(mesh, Material{}, transform, lightViewProjection, 0);
}

void MeshRenderer::DrawMeshShadow(const Mesh& mesh, const Material& material,
                                  const Transform& transform,
                                  const DirectX::XMFLOAT4X4& lightViewProjection,
                                  uint32_t textureId) {
    if (!state_->dxCommon || !state_->textureManager || !state_->shadowRootSignature ||
        !state_->shadowPSO || !IsDrawableMesh(mesh) || state_->drawIndex >= kMaxDraws) {
        return;
    }

    const Material drawMaterial =
        NormalizeMaterialForDraw(material, state_->materialReflectionsEnabled);

    D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr = 0;
    D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = 0;
    D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr = 0;
    if (!WriteShadowTransformedDrawConstants(drawMaterial, transform, lightViewProjection,
                                             objectCbAddr, sceneCbAddr, materialCbAddr)) {
        return;
    }
    SetGraphicsRootSignatureCached(state_->shadowRootSignature.Get());
    SetPipelineStateCached(state_->shadowPSO.Get());
    SubmitShadowMeshDraw(mesh, drawMaterial,
                         MeshDrawConstants{objectCbAddr, sceneCbAddr, materialCbAddr},
                         MeshVertexViewSpan{&mesh.vbView, 1, 1}, textureId);
}

bool MeshRenderer::WriteShadowTransformedDrawConstants(
    const Material& drawMaterial, const Transform& transform,
    const DirectX::XMFLOAT4X4& lightViewProjection, D3D12_GPU_VIRTUAL_ADDRESS& objectCbAddr,
    D3D12_GPU_VIRTUAL_ADDRESS& sceneCbAddr, D3D12_GPU_VIRTUAL_ADDRESS& materialCbAddr) {
    const XMMATRIX world = RendererMath::MakeWorldMatrix(transform);
    const XMMATRIX lightVP = XMLoadFloat4x4(&lightViewProjection);
    const XMMATRIX wvp = world * lightVP;
    objectCbAddr = WriteObjectConstants(wvp, world, XMMatrixIdentity());
    sceneCbAddr = WriteShadowSceneConstants(lightViewProjection);
    materialCbAddr = WriteMaterialConstants(drawMaterial);
    return objectCbAddr != 0 && sceneCbAddr != 0 && materialCbAddr != 0;
}

bool MeshRenderer::WriteShadowIdentityDrawConstants(const Material& drawMaterial,
                                                    const DirectX::XMFLOAT4X4& lightViewProjection,
                                                    D3D12_GPU_VIRTUAL_ADDRESS& objectCbAddr,
                                                    D3D12_GPU_VIRTUAL_ADDRESS& sceneCbAddr,
                                                    D3D12_GPU_VIRTUAL_ADDRESS& materialCbAddr) {
    objectCbAddr = WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(), XMMatrixIdentity());
    sceneCbAddr = WriteShadowSceneConstants(lightViewProjection);
    materialCbAddr = WriteMaterialConstants(drawMaterial);
    return objectCbAddr != 0 && sceneCbAddr != 0 && materialCbAddr != 0;
}

void MeshRenderer::DrawMeshInstancedShadow(const Mesh& mesh, const InstanceData* instances,
                                           uint32_t instanceCount,
                                           const DirectX::XMFLOAT4X4& lightViewProjection) {
    DrawMeshInstancedShadow(mesh, Material{}, instances, instanceCount, lightViewProjection, 0);
}

void MeshRenderer::DrawMeshInstancedShadow(const Mesh& mesh, const Material& material,
                                           const InstanceData* instances, uint32_t instanceCount,
                                           const DirectX::XMFLOAT4X4& lightViewProjection,
                                           uint32_t textureId) {
    if (!state_->dxCommon || !state_->textureManager || !state_->shadowRootSignature ||
        !state_->instancedShadowPSO || !IsDrawableMesh(mesh) || !instances || instanceCount == 0 ||
        state_->drawIndex >= kMaxDraws) {
        return;
    }

    const Material drawMaterial =
        NormalizeMaterialForDraw(material, state_->materialReflectionsEnabled);

    const D3D12_VERTEX_BUFFER_VIEW instanceView = WriteInstances(instances, instanceCount);
    DrawShadowInstancedWithPreparedBuffer(mesh, drawMaterial, instanceView, instanceCount,
                                          lightViewProjection, nullptr, textureId);
}

void MeshRenderer::DrawMeshInstancedShadowWithPipeline(
    uint32_t pipelineId, const Mesh& mesh, const Material& material, const InstanceData* instances,
    uint32_t instanceCount, const DirectX::XMFLOAT4X4& lightViewProjection, uint32_t textureId,
    bool opaqueShadow) {
    if (!state_->dxCommon || !state_->textureManager || !state_->shadowRootSignature ||
        pipelineId >= state_->customInstancedPipelines.size() || !IsDrawableMesh(mesh) ||
        !instances || instanceCount == 0 || state_->drawIndex >= kMaxDraws) {
        return;
    }

    const Material drawMaterial =
        NormalizeMaterialForDraw(material, state_->materialReflectionsEnabled);

    const D3D12_VERTEX_BUFFER_VIEW instanceView = WriteInstances(instances, instanceCount);

    if (state_->dxCommon->GetCommandList() == nullptr ||
        !state_->customInstancedPipelines[pipelineId].shadowPipelineStates[0]) {
        return;
    }
    const InstancedPipelineSet& pipelineSet = state_->customInstancedPipelines[pipelineId];
    const auto& shadowPipelineStates =
        opaqueShadow ? pipelineSet.opaqueShadowPipelineStates : pipelineSet.shadowPipelineStates;
    DrawShadowInstancedWithPreparedBuffer(mesh, drawMaterial, instanceView, instanceCount,
                                          lightViewProjection, &shadowPipelineStates, textureId);
}

void MeshRenderer::DrawMeshInstancedShadowWithPipeline(
    uint32_t pipelineId, const Mesh& mesh, const Material& material,
    const MeshInstanceBuffer& instanceBuffer, const DirectX::XMFLOAT4X4& lightViewProjection,
    uint32_t textureId, bool opaqueShadow) {
    if (!instanceBuffer.IsValid()) {
        return;
    }
    MarkStaticInstanceBufferUsed(instanceBuffer);
    const Material drawMaterial =
        NormalizeMaterialForDraw(material, state_->materialReflectionsEnabled);
    DrawInstancedShadowWithPreparedBuffer(pipelineId, mesh, drawMaterial, instanceBuffer.view,
                                          instanceBuffer.instanceCount, lightViewProjection,
                                          textureId, opaqueShadow);
}

bool MeshRenderer::DrawInstancedShadowWithPreparedBuffer(
    uint32_t pipelineId, const Mesh& mesh, const Material& drawMaterial,
    const D3D12_VERTEX_BUFFER_VIEW& instanceView, uint32_t instanceCount,
    const DirectX::XMFLOAT4X4& lightViewProjection, uint32_t textureId, bool opaqueShadow) {
    if (pipelineId >= state_->customInstancedPipelines.size()) {
        return false;
    }
    if (!state_->dxCommon || state_->dxCommon->GetCommandList() == nullptr ||
        !state_->customInstancedPipelines[pipelineId].shadowPipelineStates[0]) {
        return false;
    }
    const InstancedPipelineSet& pipelineSet = state_->customInstancedPipelines[pipelineId];
    const auto& shadowPipelineStates =
        opaqueShadow ? pipelineSet.opaqueShadowPipelineStates : pipelineSet.shadowPipelineStates;
    return DrawShadowInstancedWithPreparedBuffer(mesh, drawMaterial, instanceView, instanceCount,
                                                 lightViewProjection, &shadowPipelineStates,
                                                 textureId);
}

bool MeshRenderer::DrawShadowInstancedWithPreparedBuffer(
    const Mesh& mesh, const Material& drawMaterial, const D3D12_VERTEX_BUFFER_VIEW& instanceView,
    uint32_t instanceCount, const DirectX::XMFLOAT4X4& lightViewProjection,
    const PipelineStateArray* pipelineStates, uint32_t textureId) {
    if (!state_->dxCommon || !state_->textureManager || !state_->shadowRootSignature ||
        !IsDrawableMesh(mesh) || instanceCount == 0 || instanceView.BufferLocation == 0 ||
        state_->drawIndex >= kMaxDraws) {
        return false;
    }

    D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr = 0;
    D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = 0;
    D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr = 0;
    if (!WriteShadowIdentityDrawConstants(drawMaterial, lightViewProjection, objectCbAddr,
                                          sceneCbAddr, materialCbAddr)) {
        return false;
    }

    SetGraphicsRootSignatureCached(state_->shadowRootSignature.Get());
    if (pipelineStates) {
        if (state_->dxCommon->GetCommandList() == nullptr || !(*pipelineStates)[0]) {
            return false;
        }
        if (!SetInstancedShadowPipelineForMaterial(*pipelineStates, drawMaterial)) {
            return false;
        }
    } else {
        if (!state_->instancedShadowPSO) {
            return false;
        }
        SetPipelineStateCached(state_->instancedShadowPSO.Get());
    }
    const D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, instanceView};
    SubmitShadowMeshDraw(mesh, drawMaterial,
                         MeshDrawConstants{objectCbAddr, sceneCbAddr, materialCbAddr},
                         MeshVertexViewSpan{views, 2, instanceCount}, textureId);
    return true;
}

#include "model/MeshRenderer.h"

#include "core/Numeric.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"
#include "internal/MeshRendererInternal.h"
#include "internal/RendererMaterialUtils.h"
#include "internal/RendererShadowMapUtils.h"
#include "model/RendererMath.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <array>
#include <cstring>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {
using Numeric::ClampFinite;
using Numeric::FiniteOr;
using RendererMaterialUtils::IsDrawableMesh;
using RendererMaterialUtils::PipelineVariantIndex;
using RendererMaterialUtils::ResolveBaseColorTextureId;
using RendererMaterialUtils::ResolveNormalTextureId;

XMFLOAT4 SanitizeFloat4(const XMFLOAT4& value, const XMFLOAT4& fallback) {
    return {FiniteOr(value.x, fallback.x), FiniteOr(value.y, fallback.y),
            FiniteOr(value.z, fallback.z), FiniteOr(value.w, fallback.w)};
}

} // namespace

void MeshRenderer::DrawMesh(const Mesh& mesh, const Material& material, const Transform& transform,
                            const Camera& camera, uint32_t textureId, uint32_t normalTextureId) {
    DrawForwardMeshWithPipelineStates(mesh, material, transform, camera, nullptr, textureId,
                                      normalTextureId);
}

void MeshRenderer::DrawMeshWithPipeline(uint32_t pipelineId, const Mesh& mesh,
                                        const Material& material, const Transform& transform,
                                        const Camera& camera, uint32_t textureId,
                                        uint32_t normalTextureId) {
    if (pipelineId >= state_->customPipelines.size()) {
        return;
    }
    DrawForwardMeshWithPipelineStates(mesh, material, transform, camera,
                                      &state_->customPipelines[pipelineId].pipelineStates,
                                      textureId, normalTextureId);
}

void MeshRenderer::DrawMeshWithPipelineHandles(uint32_t pipelineId, const Mesh& mesh,
                                               const Material& material, const Transform& transform,
                                               const Camera& camera,
                                               D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
                                               D3D12_GPU_DESCRIPTOR_HANDLE normalTextureHandle) {
    if (pipelineId >= state_->customPipelines.size()) {
        return;
    }
    DrawForwardMeshWithPipelineHandles(mesh, material, transform, camera,
                                       state_->customPipelines[pipelineId].pipelineStates,
                                       textureHandle, normalTextureHandle);
}

bool MeshRenderer::DrawForwardMeshWithPipelineStates(const Mesh& mesh, const Material& material,
                                                     const Transform& transform,
                                                     const Camera& camera,
                                                     const PipelineStateArray* pipelineStates,
                                                     uint32_t textureId, uint32_t normalTextureId) {
    Material drawMaterial{};
    D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr = 0;
    D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = 0;
    D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr = 0;
    if (!PrepareForwardMeshDrawConstants(mesh, material, transform, camera, drawMaterial,
                                         objectCbAddr, sceneCbAddr, materialCbAddr)) {
        return false;
    }

    SetGraphicsRootSignatureCached(state_->rootSignature.Get());
    const bool pipelineReady = pipelineStates
                                   ? SetPipelineForMaterial(*pipelineStates, drawMaterial)
                                   : SetPipelineForMaterial(drawMaterial);
    if (!pipelineReady) {
        return false;
    }
    SubmitForwardMeshDraw(
        mesh, drawMaterial, MeshDrawConstants{objectCbAddr, sceneCbAddr, materialCbAddr},
        MeshVertexViewSpan{&mesh.vbView, 1, 1}, ForwardTextureIds{textureId, normalTextureId});
    return true;
}

bool MeshRenderer::DrawForwardMeshWithPipelineHandles(
    const Mesh& mesh, const Material& material, const Transform& transform, const Camera& camera,
    const PipelineStateArray& pipelineStates, D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE normalTextureHandle) {
    Material drawMaterial{};
    D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr = 0;
    D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = 0;
    D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr = 0;
    if (!PrepareForwardMeshDrawConstants(mesh, material, transform, camera, drawMaterial,
                                         objectCbAddr, sceneCbAddr, materialCbAddr)) {
        return false;
    }

    SetGraphicsRootSignatureCached(state_->rootSignature.Get());
    if (!SetPipelineForMaterial(pipelineStates, drawMaterial)) {
        return false;
    }
    SubmitForwardMeshDrawWithHandles(mesh,
                                     MeshDrawConstants{objectCbAddr, sceneCbAddr, materialCbAddr},
                                     MeshVertexViewSpan{&mesh.vbView, 1, 1},
                                     ForwardTextureHandles{textureHandle, normalTextureHandle});
    return true;
}

bool MeshRenderer::PrepareForwardMeshDrawConstants(const Mesh& mesh, const Material& material,
                                                   const Transform& transform, const Camera& camera,
                                                   Material& drawMaterial,
                                                   D3D12_GPU_VIRTUAL_ADDRESS& objectCbAddr,
                                                   D3D12_GPU_VIRTUAL_ADDRESS& sceneCbAddr,
                                                   D3D12_GPU_VIRTUAL_ADDRESS& materialCbAddr) {
    if (!state_->dxCommon || !state_->textureManager || !state_->rootSignature ||
        !IsDrawableMesh(mesh) || state_->drawIndex >= kMaxDraws) {
        return false;
    }
    drawMaterial = NormalizeMaterialForDraw(material, state_->materialReflectionsEnabled);
    return WriteForwardTransformedDrawConstants(drawMaterial, transform, camera, objectCbAddr,
                                                sceneCbAddr, materialCbAddr);
}

bool MeshRenderer::WriteForwardTransformedDrawConstants(const Material& drawMaterial,
                                                        const Transform& transform,
                                                        const Camera& camera,
                                                        D3D12_GPU_VIRTUAL_ADDRESS& objectCbAddr,
                                                        D3D12_GPU_VIRTUAL_ADDRESS& sceneCbAddr,
                                                        D3D12_GPU_VIRTUAL_ADDRESS& materialCbAddr) {
    const XMMATRIX world = RendererMath::MakeWorldMatrix(transform);
    const XMMATRIX worldInverseTranspose = RendererMath::MakeSafeInverseTranspose(world);
    const XMMATRIX wvp = world * camera.GetView() * camera.GetProj();
    objectCbAddr = WriteObjectConstants(wvp, world, worldInverseTranspose);
    sceneCbAddr = WriteSceneConstants(camera);
    materialCbAddr = WriteMaterialConstants(drawMaterial);
    return objectCbAddr != 0 && sceneCbAddr != 0 && materialCbAddr != 0;
}

bool MeshRenderer::WriteForwardIdentityDrawConstants(const Material& drawMaterial,
                                                     const Camera& camera,
                                                     D3D12_GPU_VIRTUAL_ADDRESS& objectCbAddr,
                                                     D3D12_GPU_VIRTUAL_ADDRESS& sceneCbAddr,
                                                     D3D12_GPU_VIRTUAL_ADDRESS& materialCbAddr) {
    objectCbAddr = WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(), XMMatrixIdentity());
    sceneCbAddr = WriteSceneConstants(camera);
    materialCbAddr = WriteMaterialConstants(drawMaterial);
    return objectCbAddr != 0 && sceneCbAddr != 0 && materialCbAddr != 0;
}

void MeshRenderer::DrawMeshInstanced(const Mesh& mesh, const Material& material,
                                     const InstanceData* instances, uint32_t instanceCount,
                                     const Camera& camera, uint32_t textureId,
                                     uint32_t normalTextureId) {
    if (!state_->dxCommon || !state_->textureManager || !state_->rootSignature ||
        !IsDrawableMesh(mesh) || !instances || instanceCount == 0 ||
        state_->drawIndex >= kMaxDraws) {
        return;
    }

    const Material drawMaterial =
        NormalizeMaterialForDraw(material, state_->materialReflectionsEnabled);

    const D3D12_VERTEX_BUFFER_VIEW instanceView = WriteInstances(instances, instanceCount);
    DrawForwardInstancedWithPreparedBuffer(mesh, drawMaterial, instanceView, instanceCount, camera,
                                           nullptr, textureId, normalTextureId);
}

void MeshRenderer::DrawMeshInstancedWithPipeline(uint32_t pipelineId, const Mesh& mesh,
                                                 const Material& material,
                                                 const InstanceData* instances,
                                                 uint32_t instanceCount, const Camera& camera,
                                                 uint32_t textureId, uint32_t normalTextureId) {
    if (!state_->dxCommon || !state_->textureManager || !state_->rootSignature ||
        pipelineId >= state_->customInstancedPipelines.size() || !IsDrawableMesh(mesh) ||
        !instances || instanceCount == 0 || state_->drawIndex >= kMaxDraws) {
        return;
    }

    const Material drawMaterial =
        NormalizeMaterialForDraw(material, state_->materialReflectionsEnabled);

    const D3D12_VERTEX_BUFFER_VIEW instanceView = WriteInstances(instances, instanceCount);
    DrawForwardInstancedWithPreparedBuffer(
        mesh, drawMaterial, instanceView, instanceCount, camera,
        &state_->customInstancedPipelines[pipelineId].pipelineStates, textureId, normalTextureId);
}

void MeshRenderer::DrawMeshInstancedWithPipeline(uint32_t pipelineId, const Mesh& mesh,
                                                 const Material& material,
                                                 const MeshInstanceBuffer& instanceBuffer,
                                                 const Camera& camera, uint32_t textureId,
                                                 uint32_t normalTextureId) {
    if (!instanceBuffer.IsValid()) {
        return;
    }
    MarkStaticInstanceBufferUsed(instanceBuffer);
    const Material drawMaterial =
        NormalizeMaterialForDraw(material, state_->materialReflectionsEnabled);
    DrawInstancedWithPreparedBuffer(pipelineId, mesh, drawMaterial, instanceBuffer.view,
                                    instanceBuffer.instanceCount, camera, textureId,
                                    normalTextureId);
}

bool MeshRenderer::DrawInstancedWithPreparedBuffer(uint32_t pipelineId, const Mesh& mesh,
                                                   const Material& drawMaterial,
                                                   const D3D12_VERTEX_BUFFER_VIEW& instanceView,
                                                   uint32_t instanceCount, const Camera& camera,
                                                   uint32_t textureId, uint32_t normalTextureId) {
    if (pipelineId >= state_->customInstancedPipelines.size()) {
        return false;
    }
    return DrawForwardInstancedWithPreparedBuffer(
        mesh, drawMaterial, instanceView, instanceCount, camera,
        &state_->customInstancedPipelines[pipelineId].pipelineStates, textureId, normalTextureId);
}

bool MeshRenderer::DrawForwardInstancedWithPreparedBuffer(
    const Mesh& mesh, const Material& drawMaterial, const D3D12_VERTEX_BUFFER_VIEW& instanceView,
    uint32_t instanceCount, const Camera& camera, const PipelineStateArray* pipelineStates,
    uint32_t textureId, uint32_t normalTextureId) {
    if (!state_->dxCommon || !state_->textureManager || !state_->rootSignature ||
        !IsDrawableMesh(mesh) || instanceCount == 0 || instanceView.BufferLocation == 0 ||
        state_->drawIndex >= kMaxDraws) {
        return false;
    }

    D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr = 0;
    D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = 0;
    D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr = 0;
    if (!WriteForwardIdentityDrawConstants(drawMaterial, camera, objectCbAddr, sceneCbAddr,
                                           materialCbAddr)) {
        return false;
    }

    SetGraphicsRootSignatureCached(state_->rootSignature.Get());
    const bool pipelineReady = pipelineStates
                                   ? SetInstancedPipelineForMaterial(*pipelineStates, drawMaterial)
                                   : SetInstancedPipelineForMaterial(drawMaterial);
    if (!pipelineReady) {
        return false;
    }
    const D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, instanceView};
    SubmitForwardMeshDraw(
        mesh, drawMaterial, MeshDrawConstants{objectCbAddr, sceneCbAddr, materialCbAddr},
        MeshVertexViewSpan{views, 2, instanceCount}, ForwardTextureIds{textureId, normalTextureId});
    return true;
}
void MeshRenderer::SetShadowMap(D3D12_GPU_DESCRIPTOR_HANDLE shadowMap,
                                const DirectX::XMFLOAT4X4& lightViewProjection,
                                const SceneShadowSettings& settings) {
    RendererShadowMapUtils::Set(state_->textureManager, shadowMap, lightViewProjection, settings,
                                state_->shadowMapGpuHandle, state_->shadowLightViewProjection,
                                state_->shadowParams, state_->shadowFilterParams);
    InvalidateConstantCaches();
}

void MeshRenderer::SetSpotLightShadowMap(D3D12_GPU_DESCRIPTOR_HANDLE shadowMap,
                                         const DirectX::XMFLOAT4X4& lightViewProjection,
                                         const SceneShadowSettings& settings) {
    RendererShadowMapUtils::Set(state_->textureManager, shadowMap, lightViewProjection, settings,
                                state_->spotLightShadowMapGpuHandle,
                                state_->spotLightViewProjection, state_->spotShadowParams,
                                state_->spotShadowFilterParams);
    InvalidateConstantCaches();
}

void MeshRenderer::SetOcclusionPyramid(D3D12_GPU_DESCRIPTOR_HANDLE depthPyramid,
                                       const DirectX::XMMATRIX& viewProjection, uint32_t width,
                                       uint32_t height, uint32_t mipCount, float depthBias) {
    if (depthPyramid.ptr == 0 || width == 0u || height == 0u || mipCount == 0u) {
        ClearOcclusionPyramid();
        return;
    }

    XMStoreFloat4x4(&state_->occlusionViewProjection, XMMatrixTranspose(viewProjection));
    state_->occlusionParams = {static_cast<float>(width), static_cast<float>(height),
                               static_cast<float>(mipCount),
                               ClampFinite(depthBias, 0.0f, 0.05f, 0.006f)};
    state_->occlusionPyramidGpuHandle = depthPyramid;
    state_->occlusionPyramidEnabled = true;
}

void MeshRenderer::ClearOcclusionPyramid() {
    state_->occlusionPyramidGpuHandle = {};
    state_->occlusionParams = {0.0f, 0.0f, 0.0f, 0.006f};
    state_->occlusionPyramidEnabled = false;
}

void MeshRenderer::SetCustomSceneParams(const DirectX::XMFLOAT4& params0,
                                        const DirectX::XMFLOAT4& params1) {
    state_->customSceneParams0 = SanitizeFloat4(params0, state_->customSceneParams0);
    state_->customSceneParams1 = SanitizeFloat4(params1, state_->customSceneParams1);
    InvalidateConstantCaches();
}

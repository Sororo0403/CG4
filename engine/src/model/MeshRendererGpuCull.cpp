#include "model/MeshRenderer.h"
#include "internal/MeshRendererInternal.h"
#include "internal/MeshRendererGpuCullInternal.h"

#include "graphics/DirectXCommon.h"
#include "graphics/SrvManager.h"
#include "internal/RendererMaterialUtils.h"
#include "model/RendererMath.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX;

namespace {

using RendererMaterialUtils::IsDrawableMesh;
using MeshRendererGpuCullInternal::BuildCameraAndMaxDistanceSq;
using MeshRendererGpuCullInternal::BuildFrustumPlanes;
using MeshRendererGpuCullInternal::BuildLocalCenterAndRadius;
using MeshRendererGpuCullInternal::BuildLodCullArgs;
using MeshRendererGpuCullInternal::BuildLodDistanceBreaks;
using MeshRendererGpuCullInternal::BuildSingleCullArgs;
using MeshRendererGpuCullInternal::ExecuteLodGpuCull;
using MeshRendererGpuCullInternal::ExecuteSingleGpuCull;
using MeshRendererGpuCullInternal::IsDistanceCullEnabled;
using MeshRendererGpuCullInternal::IsMinDistanceCullEnabled;
using MeshRendererGpuCullInternal::MeshGpuCullArgsConstants;
using MeshRendererGpuCullInternal::MeshGpuCullConstants;
using MeshRendererGpuCullInternal::MeshGpuLodCullArgsConstants;
using MeshRendererGpuCullInternal::MeshGpuLodCullConstants;

XMFLOAT4 DefaultCullOcclusionParams() {
    return {0.0f, 0.0f, 0.0f, 0.006f};
}

XMFLOAT3 MakeFiniteOrigin(const XMFLOAT3 &origin) {
    return {std::isfinite(origin.x) ? origin.x : 0.0f,
            std::isfinite(origin.y) ? origin.y : 0.0f,
            std::isfinite(origin.z) ? origin.z : 0.0f};
}

MeshGpuCullConstants BuildSingleGpuCullConstants(
    const XMMATRIX &cullViewProjection, const XMFLOAT3 &cullOrigin,
    const MeshGpuCullBounds &localBounds,
    const XMFLOAT4X4 &occlusionViewProjection,
    const XMFLOAT4 &occlusionParams, uint32_t instanceCount,
    float maxDistance, float minDistance) {
    MeshGpuCullConstants constants{};
    BuildFrustumPlanes(cullViewProjection, constants.frustumPlanes);
    constants.cameraAndMaxDistanceSq =
        BuildCameraAndMaxDistanceSq(cullOrigin, maxDistance);
    constants.localCenterAndRadius = BuildLocalCenterAndRadius(localBounds);
    constants.occlusionViewProjection = occlusionViewProjection;
    constants.occlusionParams = occlusionParams;
    constants.instanceCount = instanceCount;
    constants.enableDistanceCull = IsDistanceCullEnabled(maxDistance);
    const float safeMinDistance =
        std::isfinite(minDistance) ? (std::max)(minDistance, 0.0f) : 0.0f;
    constants.minDistanceSq = safeMinDistance * safeMinDistance;
    constants.enableMinDistanceCull = IsMinDistanceCullEnabled(minDistance);
    return constants;
}

MeshGpuLodCullConstants BuildLodGpuCullConstants(
    const XMMATRIX &cullViewProjection, const XMFLOAT3 &cullOrigin,
    const XMFLOAT3 &lodOrigin, const MeshGpuCullBounds &localBounds,
    const std::array<float, kMeshGpuCullLodCount - 1u> &distanceBreaks,
    const XMFLOAT4X4 &occlusionViewProjection,
    const XMFLOAT4 &occlusionParams, uint32_t instanceCount, uint32_t lodBias,
    float maxDistance) {
    MeshGpuLodCullConstants constants{};
    BuildFrustumPlanes(cullViewProjection, constants.frustumPlanes);
    constants.cameraAndMaxDistanceSq =
        BuildCameraAndMaxDistanceSq(cullOrigin, maxDistance);
    constants.localCenterAndRadius = BuildLocalCenterAndRadius(localBounds);
    constants.lodOriginAndBias = {lodOrigin.x, lodOrigin.y, lodOrigin.z,
                                  static_cast<float>(lodBias)};
    constants.lodDistanceBreaks = BuildLodDistanceBreaks(distanceBreaks);
    constants.occlusionViewProjection = occlusionViewProjection;
    constants.occlusionParams = occlusionParams;
    constants.instanceCount = instanceCount;
    constants.enableDistanceCull = IsDistanceCullEnabled(maxDistance);
    constants.lodBias = (std::min)(lodBias, kMeshGpuCullLodCount - 1u);
    return constants;
}

template <typename TCullConstants, typename TArgsConstants>
bool WriteGpuCullConstantBuffers(UploadRingBuffer &uploadBuffer,
                                 const TCullConstants &cullConstants,
                                 const TArgsConstants &argsConstants,
                                 D3D12_GPU_VIRTUAL_ADDRESS &cullCb,
                                 D3D12_GPU_VIRTUAL_ADDRESS &argsCb) {
    cullCb = uploadBuffer.Write(cullConstants).gpu;
    argsCb = uploadBuffer.Write(argsConstants).gpu;
    return cullCb != 0 && argsCb != 0;
}

bool ShouldUseGpuCull(uint32_t instanceCount) {
    constexpr uint32_t kMinGpuCullInstances = 33u;
    return instanceCount >= kMinGpuCullInstances;
}

} // namespace

bool MeshRenderer::DrawMeshInstancedGpuCulledWithPipeline(
    uint32_t pipelineId, const Mesh &mesh, const Material &material,
    const MeshInstanceBuffer &sourceInstances, MeshGpuCullBuffer &cullBuffer,
    const MeshGpuCullBounds &localBounds, const Camera &camera,
    float maxDistance, uint32_t textureId, uint32_t normalTextureId,
    float minDistance) {
    if (!state_->dxCommon || !state_->textureManager || !state_->srvManager || !state_->rootSignature ||
        !state_->gpuCullRootSignature || !state_->gpuCullPSO || !state_->gpuCullArgsPSO ||
        !state_->gpuCullCommandSignature ||
        pipelineId >= state_->customInstancedPipelines.size() ||
        !IsDrawableMesh(mesh) || !sourceInstances.IsValid() ||
        state_->drawIndex >= kMaxDraws) {
        return false;
    }
    if (!ShouldUseGpuCull(sourceInstances.instanceCount)) {
        return false;
    }
    MarkStaticInstanceBufferUsed(sourceInstances);
    const XMFLOAT3 cameraPosition = camera.GetPosition();
    ID3D12GraphicsCommandList *cmd = nullptr;
    if (!DispatchSingleGpuCull(
            mesh, sourceInstances, cullBuffer, localBounds,
            camera.GetViewProjection(), cameraPosition,
            state_->occlusionPyramidEnabled ? state_->occlusionParams
                                            : DefaultCullOcclusionParams(),
            maxDistance, minDistance, cmd)) {
        return false;
    }

    if (!BindGpuCulledForwardDrawState(pipelineId, material, camera, textureId,
                                       normalTextureId)) {
        return false;
    }
    ExecuteGpuCulledMeshDraw(cmd, mesh, cullBuffer);
    return true;
}

bool MeshRenderer::DrawMeshInstancedGpuLodCulledWithPipeline(
    uint32_t pipelineId,
    const std::array<const Mesh *, kMeshGpuCullLodCount> &lodMeshes,
    const Material &material, const MeshInstanceBuffer &sourceInstances,
    MeshGpuLodCullBuffer &cullBuffer, const MeshGpuCullBounds &localBounds,
    const Camera &camera,
    const std::array<float, kMeshGpuCullLodCount - 1u> &distanceBreaks,
    uint32_t lodBias, float maxDistance, uint32_t textureId,
    uint32_t normalTextureId) {
    if (!state_->dxCommon || !state_->textureManager || !state_->srvManager || !state_->rootSignature ||
        !state_->gpuLodCullRootSignature || !state_->gpuLodCullPSO ||
        !state_->gpuLodCullArgsPSO || !state_->gpuCullCommandSignature ||
        pipelineId >= state_->customInstancedPipelines.size() ||
        !sourceInstances.IsValid() || state_->drawIndex >= kMaxDraws) {
        return false;
    }
    if (!ShouldUseGpuCull(sourceInstances.instanceCount)) {
        return false;
    }
    MarkStaticInstanceBufferUsed(sourceInstances);
    for (const Mesh *mesh : lodMeshes) {
        if (mesh == nullptr || !IsDrawableMesh(*mesh)) {
            return false;
        }
    }
    const XMFLOAT3 cameraPosition = camera.GetPosition();
    ID3D12GraphicsCommandList *cmd = nullptr;
    if (!DispatchLodGpuCull(
            lodMeshes, sourceInstances, cullBuffer, localBounds,
            camera.GetViewProjection(), cameraPosition, cameraPosition,
            distanceBreaks,
            state_->occlusionPyramidEnabled ? state_->occlusionParams
                                            : DefaultCullOcclusionParams(),
            lodBias, maxDistance, cmd)) {
        return false;
    }

    if (!BindGpuCulledForwardDrawState(pipelineId, material, camera, textureId,
                                       normalTextureId)) {
        return false;
    }
    return ExecuteGpuLodCulledMeshDraws(cmd, lodMeshes, cullBuffer);
}

bool MeshRenderer::DrawMeshInstancedGpuCulledShadowWithPipeline(
    uint32_t pipelineId, const Mesh &mesh, const Material &material,
    const MeshInstanceBuffer &sourceInstances, MeshGpuCullBuffer &cullBuffer,
    const MeshGpuCullBounds &localBounds,
    const DirectX::XMFLOAT4X4 &lightViewProjection,
    const DirectX::XMFLOAT3 &cullOrigin, float maxDistance,
    uint32_t textureId, bool opaqueShadow, float minDistance) {
    if (!state_->dxCommon || !state_->textureManager || !state_->srvManager ||
        !state_->shadowRootSignature || !state_->gpuCullRootSignature || !state_->gpuCullPSO ||
        !state_->gpuCullArgsPSO || !state_->gpuCullCommandSignature ||
        pipelineId >= state_->customInstancedPipelines.size() ||
        !state_->customInstancedPipelines[pipelineId].shadowPipelineStates[0] ||
        !IsDrawableMesh(mesh) || !sourceInstances.IsValid() ||
        state_->drawIndex >= kMaxDraws) {
        return false;
    }
    if (!ShouldUseGpuCull(sourceInstances.instanceCount)) {
        return false;
    }
    MarkStaticInstanceBufferUsed(sourceInstances);
    ID3D12GraphicsCommandList *cmd = nullptr;
    if (!DispatchSingleGpuCull(
            mesh, sourceInstances, cullBuffer, localBounds,
            XMLoadFloat4x4(&lightViewProjection), cullOrigin,
            DefaultCullOcclusionParams(), maxDistance, minDistance, cmd)) {
        return false;
    }

    if (!BindGpuCulledShadowDrawState(pipelineId, material,
                                      lightViewProjection, textureId,
                                      opaqueShadow)) {
        return false;
    }
    ExecuteGpuCulledMeshDraw(cmd, mesh, cullBuffer);
    return true;
}

bool MeshRenderer::DrawMeshInstancedGpuLodCulledShadowWithPipeline(
    uint32_t pipelineId,
    const std::array<const Mesh *, kMeshGpuCullLodCount> &lodMeshes,
    const Material &material, const MeshInstanceBuffer &sourceInstances,
    MeshGpuLodCullBuffer &cullBuffer, const MeshGpuCullBounds &localBounds,
    const DirectX::XMFLOAT4X4 &lightViewProjection,
    const DirectX::XMFLOAT3 &lodOrigin,
    const std::array<float, kMeshGpuCullLodCount - 1u> &distanceBreaks,
    uint32_t lodBias, float maxDistance, uint32_t textureId,
    bool opaqueShadow) {
    if (!state_->dxCommon || !state_->textureManager || !state_->srvManager ||
        !state_->shadowRootSignature || !state_->gpuLodCullRootSignature ||
        !state_->gpuLodCullPSO || !state_->gpuLodCullArgsPSO ||
        !state_->gpuCullCommandSignature ||
        pipelineId >= state_->customInstancedPipelines.size() ||
        !state_->customInstancedPipelines[pipelineId].shadowPipelineStates[0] ||
        !sourceInstances.IsValid() || state_->drawIndex >= kMaxDraws) {
        return false;
    }
    if (!ShouldUseGpuCull(sourceInstances.instanceCount)) {
        return false;
    }
    MarkStaticInstanceBufferUsed(sourceInstances);
    for (const Mesh *mesh : lodMeshes) {
        if (mesh == nullptr || !IsDrawableMesh(*mesh)) {
            return false;
        }
    }
    ID3D12GraphicsCommandList *cmd = nullptr;
    if (!DispatchLodGpuCull(
            lodMeshes, sourceInstances, cullBuffer, localBounds,
            XMLoadFloat4x4(&lightViewProjection), lodOrigin,
            MakeFiniteOrigin(lodOrigin), distanceBreaks,
            DefaultCullOcclusionParams(), lodBias, maxDistance, cmd)) {
        return false;
    }

    if (!BindGpuCulledShadowDrawState(pipelineId, material,
                                      lightViewProjection, textureId,
                                      opaqueShadow)) {
        return false;
    }
    return ExecuteGpuLodCulledMeshDraws(cmd, lodMeshes, cullBuffer);
}

bool MeshRenderer::DispatchSingleGpuCull(
    const Mesh &mesh, const MeshInstanceBuffer &sourceInstances,
    MeshGpuCullBuffer &cullBuffer, const MeshGpuCullBounds &localBounds,
    const XMMATRIX &cullViewProjection, const XMFLOAT3 &cullOrigin,
    const XMFLOAT4 &occlusionParams, float maxDistance,
    float minDistance,
    ID3D12GraphicsCommandList *&commandList) {
    ID3D12DescriptorHeap *heap = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE occlusionHandle{};
    if (!PrepareGpuCullDispatch(sourceInstances, cullBuffer, commandList, heap,
                                occlusionHandle)) {
        return false;
    }

    const MeshGpuCullConstants cullConstants = BuildSingleGpuCullConstants(
        cullViewProjection, cullOrigin, localBounds,
        state_->occlusionViewProjection, occlusionParams,
        sourceInstances.instanceCount, maxDistance, minDistance);
    const MeshGpuCullArgsConstants argsConstants =
        BuildSingleCullArgs(mesh, sourceInstances.instanceCount);

    D3D12_GPU_VIRTUAL_ADDRESS cullCb = 0;
    D3D12_GPU_VIRTUAL_ADDRESS argsCb = 0;
    if (!WriteGpuCullConstantBuffers(state_->uploadBuffer, cullConstants,
                                     argsConstants, cullCb, argsCb)) {
        return false;
    }

    ExecuteSingleGpuCull(commandList, heap, state_->gpuCullRootSignature.Get(),
                         state_->gpuCullPSO.Get(), state_->gpuCullArgsPSO.Get(),
                         occlusionHandle, sourceInstances, cullBuffer, cullCb,
                         argsCb);
    return true;
}

bool MeshRenderer::DispatchLodGpuCull(
    const std::array<const Mesh *, kMeshGpuCullLodCount> &lodMeshes,
    const MeshInstanceBuffer &sourceInstances,
    MeshGpuLodCullBuffer &cullBuffer,
    const MeshGpuCullBounds &localBounds,
    const XMMATRIX &cullViewProjection, const XMFLOAT3 &cullOrigin,
    const XMFLOAT3 &lodOrigin,
    const std::array<float, kMeshGpuCullLodCount - 1u> &distanceBreaks,
    const XMFLOAT4 &occlusionParams, uint32_t lodBias, float maxDistance,
    ID3D12GraphicsCommandList *&commandList) {
    ID3D12DescriptorHeap *heap = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE occlusionHandle{};
    if (!PrepareGpuLodCullDispatch(sourceInstances, cullBuffer, commandList,
                                   heap, occlusionHandle)) {
        return false;
    }

    const MeshGpuLodCullConstants cullConstants = BuildLodGpuCullConstants(
        cullViewProjection, cullOrigin, lodOrigin, localBounds, distanceBreaks,
        state_->occlusionViewProjection, occlusionParams,
        sourceInstances.instanceCount, lodBias, maxDistance);
    const MeshGpuLodCullArgsConstants argsConstants =
        BuildLodCullArgs(lodMeshes, sourceInstances.instanceCount);

    D3D12_GPU_VIRTUAL_ADDRESS cullCb = 0;
    D3D12_GPU_VIRTUAL_ADDRESS argsCb = 0;
    if (!WriteGpuCullConstantBuffers(state_->uploadBuffer, cullConstants,
                                     argsConstants, cullCb, argsCb)) {
        return false;
    }

    ExecuteLodGpuCull(commandList, heap, state_->gpuLodCullRootSignature.Get(),
                      state_->gpuLodCullPSO.Get(),
                      state_->gpuLodCullArgsPSO.Get(), occlusionHandle,
                      sourceInstances, cullBuffer, cullCb, argsCb);
    return true;
}

bool MeshRenderer::PrepareGpuCullDispatch(
    const MeshInstanceBuffer &sourceInstances, MeshGpuCullBuffer &buffer,
    ID3D12GraphicsCommandList *&commandList,
    ID3D12DescriptorHeap *&descriptorHeap,
    D3D12_GPU_DESCRIPTOR_HANDLE &occlusionHandle) {
    if (!EnsureGpuCullBuffer(sourceInstances, buffer)) {
        return false;
    }

    commandList = state_->dxCommon->GetCommandList();
    descriptorHeap = state_->srvManager->GetHeap();
    occlusionHandle = GetCullOcclusionHandle();
    if (commandList == nullptr || descriptorHeap == nullptr ||
        occlusionHandle.ptr == 0) {
        return false;
    }
    return RegisterGpuCullStateRollback(buffer);
}

bool MeshRenderer::PrepareGpuLodCullDispatch(
    const MeshInstanceBuffer &sourceInstances, MeshGpuLodCullBuffer &buffer,
    ID3D12GraphicsCommandList *&commandList,
    ID3D12DescriptorHeap *&descriptorHeap,
    D3D12_GPU_DESCRIPTOR_HANDLE &occlusionHandle) {
    if (!EnsureGpuLodCullBuffer(sourceInstances, buffer)) {
        return false;
    }

    commandList = state_->dxCommon->GetCommandList();
    descriptorHeap = state_->srvManager->GetHeap();
    occlusionHandle = GetCullOcclusionHandle();
    if (commandList == nullptr || descriptorHeap == nullptr ||
        occlusionHandle.ptr == 0) {
        return false;
    }
    return RegisterGpuLodCullStateRollback(buffer);
}

bool MeshRenderer::BindGpuCulledForwardDrawState(
    uint32_t pipelineId, const Material &material, const Camera &camera,
    uint32_t textureId, uint32_t normalTextureId) {
    InvalidateCommandState();
    const Material drawMaterial =
        NormalizeMaterialForDraw(material, state_->materialReflectionsEnabled);
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(),
                             XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
        WriteMaterialConstants(drawMaterial);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || materialCbAddr == 0) {
        return false;
    }

    SetGraphicsRootSignatureCached(state_->rootSignature.Get());
    if (!SetInstancedPipelineForMaterial(
            state_->customInstancedPipelines[pipelineId].pipelineStates,
            drawMaterial)) {
        return false;
    }
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    BindForwardMaterialDescriptors(drawMaterial, textureId, normalTextureId);
    return true;
}

bool MeshRenderer::BindGpuCulledShadowDrawState(
    uint32_t pipelineId, const Material &material,
    const DirectX::XMFLOAT4X4 &lightViewProjection, uint32_t textureId,
    bool opaqueShadow) {
    InvalidateCommandState();
    const Material drawMaterial =
        NormalizeMaterialForDraw(material, state_->materialReflectionsEnabled);
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(),
                             XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr =
        WriteShadowSceneConstants(lightViewProjection);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
        WriteMaterialConstants(drawMaterial);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || materialCbAddr == 0) {
        return false;
    }

    SetGraphicsRootSignatureCached(state_->shadowRootSignature.Get());
    const InstancedPipelineSet &pipelineSet =
        state_->customInstancedPipelines[pipelineId];
    const auto &shadowPipelineStates =
        opaqueShadow ? pipelineSet.opaqueShadowPipelineStates
                     : pipelineSet.shadowPipelineStates;
    if (!SetInstancedShadowPipelineForMaterial(shadowPipelineStates,
                                               drawMaterial)) {
        return false;
    }
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    BindShadowMaterialDescriptor(drawMaterial, textureId);
    return true;
}

void MeshRenderer::ExecuteGpuCulledMeshDraw(
    ID3D12GraphicsCommandList *commandList, const Mesh &mesh,
    const MeshGpuCullBuffer &cullBuffer) {
    const D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, cullBuffer.outputView};
    IASetVertexBuffersCached(0, 2, views);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    commandList->ExecuteIndirect(state_->gpuCullCommandSignature.Get(), 1,
                                 cullBuffer.drawArgsResource.Get(), 0,
                                 nullptr, 0);
    ++state_->drawIndex;
}

bool MeshRenderer::ExecuteGpuLodCulledMeshDraws(
    ID3D12GraphicsCommandList *commandList,
    const std::array<const Mesh *, kMeshGpuCullLodCount> &lodMeshes,
    const MeshGpuLodCullBuffer &cullBuffer) {
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        if (state_->drawIndex >= kMaxDraws) {
            return true;
        }
        const D3D12_VERTEX_BUFFER_VIEW views[] = {
            lodMeshes[lod]->vbView, cullBuffer.outputViews[lod]};
        IASetVertexBuffersCached(0, 2, views);
        IASetIndexBufferCached(lodMeshes[lod]->ibView);
        IASetPrimitiveTopologyCached(lodMeshes[lod]->primitiveTopology);
        commandList->ExecuteIndirect(state_->gpuCullCommandSignature.Get(), 1,
                                     cullBuffer.drawArgsResources[lod].Get(),
                                     0, nullptr, 0);
        ++state_->drawIndex;
    }
    return true;
}

bool MeshRenderer::RegisterGpuCullStateRollback(MeshGpuCullBuffer &buffer) {
    if (state_->dxCommon == nullptr) {
        return false;
    }
    MeshGpuCullBuffer *target = &buffer;
    const D3D12_RESOURCE_STATES previousOutputState = buffer.outputState;
    const D3D12_RESOURCE_STATES previousDrawArgsState = buffer.drawArgsState;
    return state_->dxCommon->RegisterFrameRollback(
        target, [target, previousOutputState, previousDrawArgsState]() {
            target->outputState = previousOutputState;
            target->drawArgsState = previousDrawArgsState;
        });
}

bool MeshRenderer::RegisterGpuLodCullStateRollback(
    MeshGpuLodCullBuffer &buffer) {
    if (state_->dxCommon == nullptr) {
        return false;
    }
    MeshGpuLodCullBuffer *target = &buffer;
    const auto previousOutputStates = buffer.outputStates;
    const auto previousDrawArgsStates = buffer.drawArgsStates;
    return state_->dxCommon->RegisterFrameRollback(
        target, [target, previousOutputStates, previousDrawArgsStates]() {
            target->outputStates = previousOutputStates;
            target->drawArgsStates = previousDrawArgsStates;
        });
}

D3D12_GPU_DESCRIPTOR_HANDLE MeshRenderer::GetCullOcclusionHandle() const {
    if (state_->occlusionPyramidEnabled && state_->occlusionPyramidGpuHandle.ptr != 0) {
        return state_->occlusionPyramidGpuHandle;
    }
    return state_->fallbackOcclusionGpuHandle;
}

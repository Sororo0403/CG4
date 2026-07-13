#include "graphics/DirectXCommon.h"
#include "graphics/SrvManager.h"
#include "internal/MeshRendererGpuCullInternal.h"
#include "internal/MeshRendererInternal.h"
#include "internal/RendererMaterialUtils.h"
#include "model/MeshRenderer.h"
#include "model/RendererMath.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX;

namespace {

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
using MeshRendererGpuCullInternal::LodGpuCullExecutionContext;
using MeshRendererGpuCullInternal::MeshGpuCullArgsConstants;
using MeshRendererGpuCullInternal::MeshGpuCullConstants;
using MeshRendererGpuCullInternal::MeshGpuLodCullArgsConstants;
using MeshRendererGpuCullInternal::MeshGpuLodCullConstants;
using MeshRendererGpuCullInternal::SingleGpuCullExecutionContext;
using RendererMaterialUtils::IsDrawableMesh;

XMFLOAT4 DefaultCullOcclusionParams() {
    return {0.0f, 0.0f, 0.0f, 0.006f};
}

XMFLOAT3 MakeFiniteOrigin(const XMFLOAT3& origin) {
    return {std::isfinite(origin.x) ? origin.x : 0.0f, std::isfinite(origin.y) ? origin.y : 0.0f,
            std::isfinite(origin.z) ? origin.z : 0.0f};
}

MeshGpuCullConstants BuildSingleGpuCullConstants(
    const XMMATRIX& cullViewProjection, const XMFLOAT3& cullOrigin,
    const MeshGpuCullBounds& localBounds, const XMFLOAT4X4& occlusionViewProjection,
    const XMFLOAT4& occlusionParams, uint32_t instanceCount, float maxDistance, float minDistance) {
    MeshGpuCullConstants constants{};
    BuildFrustumPlanes(cullViewProjection, constants.frustumPlanes);
    constants.cameraAndMaxDistanceSq = BuildCameraAndMaxDistanceSq(cullOrigin, maxDistance);
    constants.localCenterAndRadius = BuildLocalCenterAndRadius(localBounds);
    constants.occlusionViewProjection = occlusionViewProjection;
    constants.occlusionParams = occlusionParams;
    constants.instanceCount = instanceCount;
    constants.enableDistanceCull = IsDistanceCullEnabled(maxDistance);
    const float safeMinDistance = std::isfinite(minDistance) ? (std::max)(minDistance, 0.0f) : 0.0f;
    constants.minDistanceSq = safeMinDistance * safeMinDistance;
    constants.enableMinDistanceCull = IsMinDistanceCullEnabled(minDistance);
    return constants;
}

template <typename Request>
MeshGpuLodCullConstants BuildLodGpuCullConstants(const Request& request,
                                                 const XMFLOAT4X4& occlusionViewProjection) {
    MeshGpuLodCullConstants constants{};
    BuildFrustumPlanes(request.cullViewProjection, constants.frustumPlanes);
    constants.cameraAndMaxDistanceSq =
        BuildCameraAndMaxDistanceSq(request.cullOrigin, request.maxDistance);
    constants.localCenterAndRadius = BuildLocalCenterAndRadius(*request.localBounds);
    constants.lodOriginAndBias = {request.lodOrigin.x, request.lodOrigin.y, request.lodOrigin.z,
                                  static_cast<float>(request.lodBias)};
    constants.lodDistanceBreaks = BuildLodDistanceBreaks(*request.distanceBreaks);
    constants.occlusionViewProjection = occlusionViewProjection;
    constants.occlusionParams = request.occlusionParams;
    constants.instanceCount = request.sourceInstances->instanceCount;
    constants.enableDistanceCull = IsDistanceCullEnabled(request.maxDistance);
    constants.lodBias = (std::min)(request.lodBias, kMeshGpuCullLodCount - 1u);
    return constants;
}

template <typename TCullConstants, typename TArgsConstants>
bool WriteGpuCullConstantBuffers(UploadRingBuffer& uploadBuffer,
                                 const TCullConstants& cullConstants,
                                 const TArgsConstants& argsConstants,
                                 D3D12_GPU_VIRTUAL_ADDRESS& cullCb,
                                 D3D12_GPU_VIRTUAL_ADDRESS& argsCb) {
    cullCb = uploadBuffer.Write(cullConstants).gpu;
    argsCb = uploadBuffer.Write(argsConstants).gpu;
    return cullCb != 0 && argsCb != 0;
}

bool ShouldUseGpuCull(uint32_t instanceCount) {
    constexpr uint32_t kMinGpuCullInstances = 33u;
    return instanceCount >= kMinGpuCullInstances;
}

} // namespace

bool MeshRenderer::DrawMeshInstancedGpuCulledWithPipeline(const SingleGpuCullDrawRequest& request) {
    return DrawGpuCulledForward(request);
}

bool MeshRenderer::DrawGpuCulledForward(const SingleGpuCullDrawRequest& request) {
    if (!CanDrawGpuCulledForward(request)) {
        return false;
    }
    MarkStaticInstanceBufferUsed(*request.sourceInstances);
    const XMFLOAT3 cameraPosition = request.camera->GetPosition();
    ID3D12GraphicsCommandList* cmd = nullptr;
    if (!DispatchSingleGpuCull(
            SingleGpuCullDispatchRequest{
                request.mesh, request.sourceInstances, request.cullBuffer, request.localBounds,
                request.camera->GetViewProjection(), cameraPosition,
                state_->occlusionPyramidEnabled ? state_->occlusionParams
                                                : DefaultCullOcclusionParams(),
                request.maxDistance, request.minDistance},
            cmd)) {
        return false;
    }

    if (!BindGpuCulledForwardDrawState(request.pipelineId, *request.material, *request.camera,
                                       request.textureId, request.normalTextureId)) {
        return false;
    }
    ExecuteGpuCulledMeshDraw(cmd, *request.mesh, *request.cullBuffer);
    return true;
}

bool MeshRenderer::CanDrawGpuCulledForward(const SingleGpuCullDrawRequest& request) const {
    const bool resources[] = {
        state_->dxCommon != nullptr,
        state_->textureManager != nullptr,
        state_->srvManager != nullptr,
        static_cast<bool>(state_->rootSignature),
        static_cast<bool>(state_->gpuCullRootSignature),
        static_cast<bool>(state_->gpuCullPSO),
        static_cast<bool>(state_->gpuCullArgsPSO),
        static_cast<bool>(state_->gpuCullCommandSignature),
        state_->drawIndex < kMaxDraws,
    };
    if (!std::all_of(std::begin(resources), std::end(resources),
                     [](bool value) { return value; })) {
        return false;
    }
    if (request.pipelineId >= state_->customInstancedPipelines.size() || request.mesh == nullptr ||
        request.material == nullptr || request.sourceInstances == nullptr ||
        request.cullBuffer == nullptr || request.localBounds == nullptr ||
        request.camera == nullptr) {
        return false;
    }
    return IsDrawableMesh(*request.mesh) && request.sourceInstances->IsValid() &&
           ShouldUseGpuCull(request.sourceInstances->instanceCount);
}

bool MeshRenderer::DrawMeshInstancedGpuLodCulledWithPipeline(const LodGpuCullDrawRequest& request) {
    return DrawGpuLodCulledForward(request);
}

bool MeshRenderer::DrawGpuLodCulledForward(const LodGpuCullDrawRequest& request) {
    if (!CanDrawGpuLodCulledForward(request)) {
        return false;
    }
    MarkStaticInstanceBufferUsed(*request.sourceInstances);
    for (const Mesh* mesh : *request.lodMeshes) {
        if (mesh == nullptr || !IsDrawableMesh(*mesh)) {
            return false;
        }
    }
    const XMFLOAT3 cameraPosition = request.camera->GetPosition();
    ID3D12GraphicsCommandList* cmd = nullptr;
    if (!DispatchLodGpuCull(
            LodGpuCullDispatchRequest{
                request.lodMeshes, request.sourceInstances, request.cullBuffer, request.localBounds,
                request.camera->GetViewProjection(), cameraPosition, cameraPosition,
                request.distanceBreaks,
                state_->occlusionPyramidEnabled ? state_->occlusionParams
                                                : DefaultCullOcclusionParams(),
                request.lodBias, request.maxDistance},
            cmd)) {
        return false;
    }

    if (!BindGpuCulledForwardDrawState(request.pipelineId, *request.material, *request.camera,
                                       request.textureId, request.normalTextureId)) {
        return false;
    }
    return ExecuteGpuLodCulledMeshDraws(cmd, *request.lodMeshes, *request.cullBuffer);
}

bool MeshRenderer::CanDrawGpuLodCulledForward(const LodGpuCullDrawRequest& request) const {
    const bool resources[] = {
        state_->dxCommon != nullptr,
        state_->textureManager != nullptr,
        state_->srvManager != nullptr,
        static_cast<bool>(state_->rootSignature),
        static_cast<bool>(state_->gpuLodCullRootSignature),
        static_cast<bool>(state_->gpuLodCullPSO),
        static_cast<bool>(state_->gpuLodCullArgsPSO),
        static_cast<bool>(state_->gpuCullCommandSignature),
        state_->drawIndex < kMaxDraws,
    };
    if (!std::all_of(std::begin(resources), std::end(resources),
                     [](bool value) { return value; })) {
        return false;
    }
    if (request.pipelineId >= state_->customInstancedPipelines.size() ||
        request.lodMeshes == nullptr || request.material == nullptr ||
        request.sourceInstances == nullptr || request.cullBuffer == nullptr ||
        request.localBounds == nullptr || request.camera == nullptr ||
        request.distanceBreaks == nullptr) {
        return false;
    }
    return request.sourceInstances->IsValid() &&
           ShouldUseGpuCull(request.sourceInstances->instanceCount);
}

bool MeshRenderer::DrawMeshInstancedGpuCulledShadowWithPipeline(
    const SingleGpuCullShadowDrawRequest& request) {
    return DrawGpuCulledShadow(request);
}

bool MeshRenderer::DrawGpuCulledShadow(const SingleGpuCullShadowDrawRequest& request) {
    if (!CanDrawGpuCulledShadow(request)) {
        return false;
    }
    MarkStaticInstanceBufferUsed(*request.sourceInstances);
    ID3D12GraphicsCommandList* cmd = nullptr;
    if (!DispatchSingleGpuCull(
            SingleGpuCullDispatchRequest{
                request.mesh, request.sourceInstances, request.cullBuffer, request.localBounds,
                XMLoadFloat4x4(request.lightViewProjection), request.cullOrigin,
                DefaultCullOcclusionParams(), request.maxDistance, request.minDistance},
            cmd)) {
        return false;
    }

    if (!BindGpuCulledShadowDrawState(request.pipelineId, *request.material,
                                      *request.lightViewProjection, request.textureId,
                                      request.opaqueShadow)) {
        return false;
    }
    ExecuteGpuCulledMeshDraw(cmd, *request.mesh, *request.cullBuffer);
    return true;
}

bool MeshRenderer::CanDrawGpuCulledShadow(const SingleGpuCullShadowDrawRequest& request) const {
    const bool resources[] = {
        state_->dxCommon != nullptr,
        state_->textureManager != nullptr,
        state_->srvManager != nullptr,
        static_cast<bool>(state_->shadowRootSignature),
        static_cast<bool>(state_->gpuCullRootSignature),
        static_cast<bool>(state_->gpuCullPSO),
        static_cast<bool>(state_->gpuCullArgsPSO),
        static_cast<bool>(state_->gpuCullCommandSignature),
        state_->drawIndex < kMaxDraws,
    };
    if (!std::all_of(std::begin(resources), std::end(resources),
                     [](bool value) { return value; })) {
        return false;
    }
    if (request.pipelineId >= state_->customInstancedPipelines.size() || request.mesh == nullptr ||
        request.material == nullptr || request.sourceInstances == nullptr ||
        request.cullBuffer == nullptr || request.localBounds == nullptr ||
        request.lightViewProjection == nullptr) {
        return false;
    }
    return state_->customInstancedPipelines[request.pipelineId].shadowPipelineStates[0] &&
           IsDrawableMesh(*request.mesh) && request.sourceInstances->IsValid() &&
           ShouldUseGpuCull(request.sourceInstances->instanceCount);
}

bool MeshRenderer::DrawMeshInstancedGpuLodCulledShadowWithPipeline(
    const LodGpuCullShadowDrawRequest& request) {
    return DrawGpuLodCulledShadow(request);
}

bool MeshRenderer::DrawGpuLodCulledShadow(const LodGpuCullShadowDrawRequest& request) {
    if (!CanDrawGpuLodCulledShadow(request)) {
        return false;
    }
    MarkStaticInstanceBufferUsed(*request.sourceInstances);
    for (const Mesh* mesh : *request.lodMeshes) {
        if (mesh == nullptr || !IsDrawableMesh(*mesh)) {
            return false;
        }
    }
    ID3D12GraphicsCommandList* cmd = nullptr;
    if (!DispatchLodGpuCull(
            LodGpuCullDispatchRequest{
                request.lodMeshes, request.sourceInstances, request.cullBuffer, request.localBounds,
                XMLoadFloat4x4(request.lightViewProjection), request.lodOrigin,
                MakeFiniteOrigin(request.lodOrigin), request.distanceBreaks,
                DefaultCullOcclusionParams(), request.lodBias, request.maxDistance},
            cmd)) {
        return false;
    }

    if (!BindGpuCulledShadowDrawState(request.pipelineId, *request.material,
                                      *request.lightViewProjection, request.textureId,
                                      request.opaqueShadow)) {
        return false;
    }
    return ExecuteGpuLodCulledMeshDraws(cmd, *request.lodMeshes, *request.cullBuffer);
}

bool MeshRenderer::CanDrawGpuLodCulledShadow(const LodGpuCullShadowDrawRequest& request) const {
    const bool resources[] = {
        state_->dxCommon != nullptr,
        state_->textureManager != nullptr,
        state_->srvManager != nullptr,
        static_cast<bool>(state_->shadowRootSignature),
        static_cast<bool>(state_->gpuLodCullRootSignature),
        static_cast<bool>(state_->gpuLodCullPSO),
        static_cast<bool>(state_->gpuLodCullArgsPSO),
        static_cast<bool>(state_->gpuCullCommandSignature),
        state_->drawIndex < kMaxDraws,
    };
    if (!std::all_of(std::begin(resources), std::end(resources),
                     [](bool value) { return value; })) {
        return false;
    }
    if (request.pipelineId >= state_->customInstancedPipelines.size() ||
        request.lodMeshes == nullptr || request.material == nullptr ||
        request.sourceInstances == nullptr || request.cullBuffer == nullptr ||
        request.localBounds == nullptr || request.lightViewProjection == nullptr ||
        request.distanceBreaks == nullptr) {
        return false;
    }
    return state_->customInstancedPipelines[request.pipelineId].shadowPipelineStates[0] &&
           request.sourceInstances->IsValid() &&
           ShouldUseGpuCull(request.sourceInstances->instanceCount);
}

bool MeshRenderer::DispatchSingleGpuCull(const SingleGpuCullDispatchRequest& request,
                                         ID3D12GraphicsCommandList*& commandList) {
    ID3D12DescriptorHeap* heap = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE occlusionHandle{};
    if (!PrepareGpuCullDispatch(*request.sourceInstances, *request.cullBuffer, commandList, heap,
                                occlusionHandle)) {
        return false;
    }

    const MeshGpuCullConstants cullConstants = BuildSingleGpuCullConstants(
        request.cullViewProjection, request.cullOrigin, *request.localBounds,
        state_->occlusionViewProjection, request.occlusionParams,
        request.sourceInstances->instanceCount, request.maxDistance, request.minDistance);
    const MeshGpuCullArgsConstants argsConstants =
        BuildSingleCullArgs(*request.mesh, request.sourceInstances->instanceCount);

    D3D12_GPU_VIRTUAL_ADDRESS cullCb = 0;
    D3D12_GPU_VIRTUAL_ADDRESS argsCb = 0;
    if (!WriteGpuCullConstantBuffers(state_->uploadBuffer, cullConstants, argsConstants, cullCb,
                                     argsCb)) {
        return false;
    }

    ExecuteSingleGpuCull(SingleGpuCullExecutionContext{
        commandList, heap, state_->gpuCullRootSignature.Get(), state_->gpuCullPSO.Get(),
        state_->gpuCullArgsPSO.Get(), occlusionHandle, request.sourceInstances, request.cullBuffer,
        cullCb, argsCb});
    return true;
}

bool MeshRenderer::DispatchLodGpuCull(const LodGpuCullDispatchRequest& request,
                                      ID3D12GraphicsCommandList*& commandList) {
    ID3D12DescriptorHeap* heap = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE occlusionHandle{};
    if (!PrepareGpuLodCullDispatch(*request.sourceInstances, *request.cullBuffer, commandList, heap,
                                   occlusionHandle)) {
        return false;
    }

    const MeshGpuLodCullConstants cullConstants =
        BuildLodGpuCullConstants(request, state_->occlusionViewProjection);
    const MeshGpuLodCullArgsConstants argsConstants =
        BuildLodCullArgs(*request.lodMeshes, request.sourceInstances->instanceCount);

    D3D12_GPU_VIRTUAL_ADDRESS cullCb = 0;
    D3D12_GPU_VIRTUAL_ADDRESS argsCb = 0;
    if (!WriteGpuCullConstantBuffers(state_->uploadBuffer, cullConstants, argsConstants, cullCb,
                                     argsCb)) {
        return false;
    }

    ExecuteLodGpuCull(LodGpuCullExecutionContext{
        commandList, heap, state_->gpuLodCullRootSignature.Get(), state_->gpuLodCullPSO.Get(),
        state_->gpuLodCullArgsPSO.Get(), occlusionHandle, request.sourceInstances,
        request.cullBuffer, cullCb, argsCb});
    return true;
}

bool MeshRenderer::PrepareGpuCullDispatch(const MeshInstanceBuffer& sourceInstances,
                                          MeshGpuCullBuffer& buffer,
                                          ID3D12GraphicsCommandList*& commandList,
                                          ID3D12DescriptorHeap*& descriptorHeap,
                                          D3D12_GPU_DESCRIPTOR_HANDLE& occlusionHandle) {
    if (!EnsureGpuCullBuffer(sourceInstances, buffer)) {
        return false;
    }

    commandList = state_->dxCommon->GetCommandList();
    descriptorHeap = state_->srvManager->GetHeap();
    occlusionHandle = GetCullOcclusionHandle();
    if (commandList == nullptr || descriptorHeap == nullptr || occlusionHandle.ptr == 0) {
        return false;
    }
    return RegisterGpuCullStateRollback(buffer);
}

bool MeshRenderer::PrepareGpuLodCullDispatch(const MeshInstanceBuffer& sourceInstances,
                                             MeshGpuLodCullBuffer& buffer,
                                             ID3D12GraphicsCommandList*& commandList,
                                             ID3D12DescriptorHeap*& descriptorHeap,
                                             D3D12_GPU_DESCRIPTOR_HANDLE& occlusionHandle) {
    if (!EnsureGpuLodCullBuffer(sourceInstances, buffer)) {
        return false;
    }

    commandList = state_->dxCommon->GetCommandList();
    descriptorHeap = state_->srvManager->GetHeap();
    occlusionHandle = GetCullOcclusionHandle();
    if (commandList == nullptr || descriptorHeap == nullptr || occlusionHandle.ptr == 0) {
        return false;
    }
    return RegisterGpuLodCullStateRollback(buffer);
}

bool MeshRenderer::BindGpuCulledForwardDrawState(uint32_t pipelineId, const Material& material,
                                                 const Camera& camera, uint32_t textureId,
                                                 uint32_t normalTextureId) {
    InvalidateCommandState();
    const Material drawMaterial =
        NormalizeMaterialForDraw(material, state_->materialReflectionsEnabled);
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(), XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr = WriteMaterialConstants(drawMaterial);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || materialCbAddr == 0) {
        return false;
    }

    SetGraphicsRootSignatureCached(state_->rootSignature.Get());
    if (!SetInstancedPipelineForMaterial(
            state_->customInstancedPipelines[pipelineId].pipelineStates, drawMaterial)) {
        return false;
    }
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    BindForwardMaterialDescriptors(drawMaterial, textureId, normalTextureId);
    return true;
}

bool MeshRenderer::BindGpuCulledShadowDrawState(uint32_t pipelineId, const Material& material,
                                                const DirectX::XMFLOAT4X4& lightViewProjection,
                                                uint32_t textureId, bool opaqueShadow) {
    InvalidateCommandState();
    const Material drawMaterial =
        NormalizeMaterialForDraw(material, state_->materialReflectionsEnabled);
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(), XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteShadowSceneConstants(lightViewProjection);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr = WriteMaterialConstants(drawMaterial);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || materialCbAddr == 0) {
        return false;
    }

    SetGraphicsRootSignatureCached(state_->shadowRootSignature.Get());
    const InstancedPipelineSet& pipelineSet = state_->customInstancedPipelines[pipelineId];
    const auto& shadowPipelineStates =
        opaqueShadow ? pipelineSet.opaqueShadowPipelineStates : pipelineSet.shadowPipelineStates;
    if (!SetInstancedShadowPipelineForMaterial(shadowPipelineStates, drawMaterial)) {
        return false;
    }
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    BindShadowMaterialDescriptor(drawMaterial, textureId);
    return true;
}

void MeshRenderer::ExecuteGpuCulledMeshDraw(ID3D12GraphicsCommandList* commandList,
                                            const Mesh& mesh, const MeshGpuCullBuffer& cullBuffer) {
    const D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, cullBuffer.outputView};
    IASetVertexBuffersCached(0, 2, views);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    commandList->ExecuteIndirect(state_->gpuCullCommandSignature.Get(), 1,
                                 cullBuffer.drawArgsResource.Get(), 0, nullptr, 0);
    ++state_->drawIndex;
}

bool MeshRenderer::ExecuteGpuLodCulledMeshDraws(
    ID3D12GraphicsCommandList* commandList,
    const std::array<const Mesh*, kMeshGpuCullLodCount>& lodMeshes,
    const MeshGpuLodCullBuffer& cullBuffer) {
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        if (state_->drawIndex >= kMaxDraws) {
            return true;
        }
        const D3D12_VERTEX_BUFFER_VIEW views[] = {lodMeshes[lod]->vbView,
                                                  cullBuffer.outputViews[lod]};
        IASetVertexBuffersCached(0, 2, views);
        IASetIndexBufferCached(lodMeshes[lod]->ibView);
        IASetPrimitiveTopologyCached(lodMeshes[lod]->primitiveTopology);
        commandList->ExecuteIndirect(state_->gpuCullCommandSignature.Get(), 1,
                                     cullBuffer.drawArgsResources[lod].Get(), 0, nullptr, 0);
        ++state_->drawIndex;
    }
    return true;
}

bool MeshRenderer::RegisterGpuCullStateRollback(MeshGpuCullBuffer& buffer) {
    if (state_->dxCommon == nullptr) {
        return false;
    }
    MeshGpuCullBuffer* target = &buffer;
    const D3D12_RESOURCE_STATES previousOutputState = buffer.outputState;
    const D3D12_RESOURCE_STATES previousDrawArgsState = buffer.drawArgsState;
    return state_->dxCommon->RegisterFrameRollback(
        target, [target, previousOutputState, previousDrawArgsState]() {
            target->outputState = previousOutputState;
            target->drawArgsState = previousDrawArgsState;
        });
}

bool MeshRenderer::RegisterGpuLodCullStateRollback(MeshGpuLodCullBuffer& buffer) {
    if (state_->dxCommon == nullptr) {
        return false;
    }
    MeshGpuLodCullBuffer* target = &buffer;
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

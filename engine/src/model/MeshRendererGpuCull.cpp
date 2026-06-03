#include "model/MeshRenderer.h"

#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/SrvManager.h"
#include "model/RendererMath.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace DirectX;

namespace {

struct MeshGpuCullConstants {
    XMFLOAT4 frustumPlanes[6];
    XMFLOAT4 cameraAndMaxDistanceSq;
    XMFLOAT4 localCenterAndRadius;
    XMFLOAT4X4 occlusionViewProjection;
    XMFLOAT4 occlusionParams;
    uint32_t instanceCount = 0;
    uint32_t enableDistanceCull = 0;
    uint32_t padding[2]{};
};

struct MeshGpuCullArgsConstants {
    uint32_t indexCountPerInstance = 0;
    uint32_t maxInstanceCount = 0;
    uint32_t startIndexLocation = 0;
    int32_t baseVertexLocation = 0;
    uint32_t startInstanceLocation = 0;
    uint32_t padding[3]{};
};

struct MeshGpuLodCullConstants {
    XMFLOAT4 frustumPlanes[6];
    XMFLOAT4 cameraAndMaxDistanceSq;
    XMFLOAT4 localCenterAndRadius;
    XMFLOAT4 lodOriginAndBias;
    XMFLOAT4 lodDistanceBreaks;
    XMFLOAT4X4 occlusionViewProjection;
    XMFLOAT4 occlusionParams;
    uint32_t instanceCount = 0;
    uint32_t enableDistanceCull = 0;
    uint32_t lodBias = 0;
    uint32_t paddingParam = 0;
};

struct MeshGpuLodCullArgsConstants {
    XMUINT4 indexCountPerInstance{};
    uint32_t maxInstanceCount = 0;
    uint32_t startIndexLocation = 0;
    int32_t baseVertexLocation = 0;
    uint32_t startInstanceLocation = 0;
};

XMFLOAT4 NormalizePlane(FXMVECTOR plane) {
    const XMVECTOR normal = XMVectorSetW(plane, 0.0f);
    const float length = XMVectorGetX(XMVector3Length(normal));
    if (!std::isfinite(length) || length <= 0.000001f) {
        return {0.0f, 1.0f, 0.0f, 0.0f};
    }

    XMFLOAT4 result{};
    XMStoreFloat4(&result, plane / length);
    if (!std::isfinite(result.x) || !std::isfinite(result.y) ||
        !std::isfinite(result.z) || !std::isfinite(result.w)) {
        return {0.0f, 1.0f, 0.0f, 0.0f};
    }
    return result;
}

void BuildFrustumPlanes(const XMMATRIX &viewProjection,
                        XMFLOAT4 (&planes)[6]) {
    XMFLOAT4X4 m{};
    XMStoreFloat4x4(&m, viewProjection);
    planes[0] = NormalizePlane(
        XMVectorSet(m._14 + m._11, m._24 + m._21, m._34 + m._31,
                    m._44 + m._41));
    planes[1] = NormalizePlane(
        XMVectorSet(m._14 - m._11, m._24 - m._21, m._34 - m._31,
                    m._44 - m._41));
    planes[2] = NormalizePlane(
        XMVectorSet(m._14 - m._12, m._24 - m._22, m._34 - m._32,
                    m._44 - m._42));
    planes[3] = NormalizePlane(
        XMVectorSet(m._14 + m._12, m._24 + m._22, m._34 + m._32,
                    m._44 + m._42));
    planes[4] = NormalizePlane(XMVectorSet(m._13, m._23, m._33, m._43));
    planes[5] = NormalizePlane(
        XMVectorSet(m._14 - m._13, m._24 - m._23, m._34 - m._33,
                    m._44 - m._43));
}

uint32_t ResolveTextureId(TextureManager *textureManager, uint32_t textureId,
                          uint32_t fallbackTextureId) {
    if (textureManager == nullptr) {
        return UINT32_MAX;
    }
    if (textureId != UINT32_MAX &&
        textureManager->IsValidTextureId(textureId)) {
        return textureId;
    }
    if (fallbackTextureId != UINT32_MAX &&
        textureManager->IsValidTextureId(fallbackTextureId)) {
        return fallbackTextureId;
    }
    return textureManager->GetWhiteTextureId();
}

uint32_t ResolveNormalTextureId(TextureManager *textureManager,
                                uint32_t normalTextureId) {
    const uint32_t fallbackTextureId =
        textureManager != nullptr ? textureManager->GetDefaultNormalTextureId()
                                  : UINT32_MAX;
    return ResolveTextureId(textureManager, normalTextureId,
                            fallbackTextureId);
}

uint32_t ResolveBaseColorTextureId(TextureManager *textureManager,
                                   const Material &material,
                                   uint32_t fallbackTextureId) {
    const uint32_t textureId = material.baseColorTextureId == UINT32_MAX
                                   ? fallbackTextureId
                                   : material.baseColorTextureId;
    return ResolveTextureId(textureManager, textureId, fallbackTextureId);
}

uint32_t ResolveNormalTextureId(TextureManager *textureManager,
                                const Material &material,
                                uint32_t fallbackTextureId) {
    const uint32_t textureId = material.normalTextureId == UINT32_MAX
                                   ? fallbackTextureId
                                   : material.normalTextureId;
    return ResolveNormalTextureId(textureManager, textureId);
}

bool IsDrawableMesh(const Mesh &mesh) {
    return mesh.vertexBuffer && mesh.indexBuffer && mesh.indexCount > 0 &&
           mesh.vertexStride > 0 && mesh.vbView.BufferLocation != 0 &&
           mesh.vbView.SizeInBytes > 0 &&
           mesh.vbView.StrideInBytes > 0 &&
           mesh.ibView.BufferLocation != 0 && mesh.ibView.SizeInBytes > 0 &&
           mesh.primitiveTopology != D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
}

} // namespace

bool MeshRenderer::DrawMeshInstancedGpuCulledWithPipeline(
    uint32_t pipelineId, const Mesh &mesh, const Material &material,
    const MeshInstanceBuffer &sourceInstances, MeshGpuCullBuffer &cullBuffer,
    const MeshGpuCullBounds &localBounds, const Camera &camera,
    float maxDistance, uint32_t textureId, uint32_t normalTextureId) {
    if (!dxCommon_ || !textureManager_ || !srvManager_ || !rootSignature_ ||
        !gpuCullRootSignature_ || !gpuCullPSO_ || !gpuCullArgsPSO_ ||
        !gpuCullCommandSignature_ ||
        pipelineId >= customInstancedPipelines_.size() ||
        !IsDrawableMesh(mesh) || !sourceInstances.IsValid() ||
        drawIndex_ >= kMaxDraws) {
        return false;
    }
    if (!EnsureGpuCullBuffer(sourceInstances, cullBuffer)) {
        return false;
    }

    auto *cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    const D3D12_GPU_DESCRIPTOR_HANDLE occlusionHandle =
        GetCullOcclusionHandle();
    if (cmd == nullptr || heap == nullptr || occlusionHandle.ptr == 0) {
        return false;
    }
    if (!RegisterGpuCullStateRollback(cullBuffer)) {
        return false;
    }

    MeshGpuCullConstants cullConstants{};
    BuildFrustumPlanes(camera.GetViewProjection(), cullConstants.frustumPlanes);
    const XMFLOAT3 cameraPosition = camera.GetPosition();
    const float safeMaxDistance =
        std::isfinite(maxDistance) ? (std::max)(maxDistance, 0.0f) : 0.0f;
    cullConstants.cameraAndMaxDistanceSq = {
        cameraPosition.x, cameraPosition.y, cameraPosition.z,
        safeMaxDistance * safeMaxDistance};
    const float radius =
        std::isfinite(localBounds.radius)
            ? (std::max)(localBounds.radius, 0.0001f)
            : 0.0001f;
    cullConstants.localCenterAndRadius = {
        std::isfinite(localBounds.center.x) ? localBounds.center.x : 0.0f,
        std::isfinite(localBounds.center.y) ? localBounds.center.y : 0.0f,
        std::isfinite(localBounds.center.z) ? localBounds.center.z : 0.0f,
        radius};
    cullConstants.occlusionViewProjection = occlusionViewProjection_;
    cullConstants.occlusionParams =
        occlusionPyramidEnabled_ ? occlusionParams_
                                 : XMFLOAT4{0.0f, 0.0f, 0.0f, 0.006f};
    cullConstants.instanceCount = sourceInstances.instanceCount;
    cullConstants.enableDistanceCull = safeMaxDistance > 0.0f ? 1u : 0u;

    MeshGpuCullArgsConstants argsConstants{};
    argsConstants.indexCountPerInstance = mesh.indexCount;
    argsConstants.maxInstanceCount = sourceInstances.instanceCount;
    argsConstants.startIndexLocation = 0u;
    argsConstants.baseVertexLocation = 0;
    argsConstants.startInstanceLocation = 0u;

    const D3D12_GPU_VIRTUAL_ADDRESS cullCb =
        uploadBuffer_.Write(cullConstants).gpu;
    const D3D12_GPU_VIRTUAL_ADDRESS argsCb =
        uploadBuffer_.Write(argsConstants).gpu;
    if (cullCb == 0 || argsCb == 0) {
        return false;
    }

    const UINT zeroValues[4] = {0u, 0u, 0u, 0u};
    ID3D12DescriptorHeap *heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);

    std::vector<D3D12_RESOURCE_BARRIER> beforeCullBarriers;
    beforeCullBarriers.reserve(3);
    beforeCullBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
        sourceInstances.resource.Get(),
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    if (cullBuffer.outputState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        beforeCullBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            cullBuffer.outputResource.Get(), cullBuffer.outputState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        cullBuffer.outputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (cullBuffer.drawArgsState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        beforeCullBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            cullBuffer.drawArgsResource.Get(), cullBuffer.drawArgsState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        cullBuffer.drawArgsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (!beforeCullBarriers.empty()) {
        cmd->ResourceBarrier(static_cast<UINT>(beforeCullBarriers.size()),
                             beforeCullBarriers.data());
    }

    cmd->ClearUnorderedAccessViewUint(
        cullBuffer.countUavGpuHandle, cullBuffer.countUavCpuHandle,
        cullBuffer.countResource.Get(), zeroValues, 0, nullptr);
    D3D12_RESOURCE_BARRIER countClearBarrier =
        CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.countResource.Get());
    cmd->ResourceBarrier(1, &countClearBarrier);

    cmd->SetComputeRootSignature(gpuCullRootSignature_.Get());
    cmd->SetPipelineState(gpuCullPSO_.Get());
    cmd->SetComputeRootConstantBufferView(0, cullCb);
    cmd->SetComputeRootDescriptorTable(1, cullBuffer.sourceSrvGpuHandle);
    cmd->SetComputeRootDescriptorTable(2, occlusionHandle);
    cmd->SetComputeRootDescriptorTable(3, cullBuffer.outputUavGpuHandle);
    cmd->SetComputeRootDescriptorTable(4, cullBuffer.countUavGpuHandle);
    cmd->SetComputeRootDescriptorTable(5, cullBuffer.drawArgsUavGpuHandle);
    cmd->Dispatch((sourceInstances.instanceCount + 127u) / 128u, 1u, 1u);

    D3D12_RESOURCE_BARRIER cullUavBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.outputResource.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.countResource.Get())};
    cmd->ResourceBarrier(_countof(cullUavBarriers), cullUavBarriers);

    cmd->SetPipelineState(gpuCullArgsPSO_.Get());
    cmd->SetComputeRootConstantBufferView(0, argsCb);
    cmd->Dispatch(1u, 1u, 1u);

    D3D12_RESOURCE_BARRIER drawArgsUav =
        CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.drawArgsResource.Get());
    cmd->ResourceBarrier(1, &drawArgsUav);

    D3D12_RESOURCE_BARRIER drawBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            cullBuffer.outputResource.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        CD3DX12_RESOURCE_BARRIER::Transition(
            cullBuffer.drawArgsResource.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
        CD3DX12_RESOURCE_BARRIER::Transition(
            sourceInstances.resource.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)};
    cmd->ResourceBarrier(_countof(drawBarriers), drawBarriers);
    cullBuffer.outputState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    cullBuffer.drawArgsState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

    InvalidateCommandState();
    const Material drawMaterial = NormalizeMaterialForDraw(material);
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(),
                             XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
        WriteMaterialConstants(drawMaterial);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || materialCbAddr == 0) {
        return false;
    }

    SetGraphicsRootSignatureCached(rootSignature_.Get());
    if (!SetInstancedPipelineForMaterial(
            customInstancedPipelines_[pipelineId].pipelineStates,
            drawMaterial)) {
        return false;
    }
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    SetGraphicsRootDescriptorTableCached(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));
    SetGraphicsRootDescriptorTableCached(4, shadowMapGpuHandle_);
    SetGraphicsRootDescriptorTableCached(
        5, textureManager_->GetGpuHandle(
               ResolveNormalTextureId(textureManager_, drawMaterial,
                                      normalTextureId)));
    D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, cullBuffer.outputView};
    IASetVertexBuffersCached(0, 2, views);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    cmd->ExecuteIndirect(gpuCullCommandSignature_.Get(), 1,
                         cullBuffer.drawArgsResource.Get(), 0, nullptr, 0);

    ++drawIndex_;
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
    if (!dxCommon_ || !textureManager_ || !srvManager_ || !rootSignature_ ||
        !gpuLodCullRootSignature_ || !gpuLodCullPSO_ ||
        !gpuLodCullArgsPSO_ || !gpuCullCommandSignature_ ||
        pipelineId >= customInstancedPipelines_.size() ||
        !sourceInstances.IsValid() || drawIndex_ >= kMaxDraws) {
        return false;
    }
    for (const Mesh *mesh : lodMeshes) {
        if (mesh == nullptr || !IsDrawableMesh(*mesh)) {
            return false;
        }
    }
    if (!EnsureGpuLodCullBuffer(sourceInstances, cullBuffer)) {
        return false;
    }

    auto *cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    const D3D12_GPU_DESCRIPTOR_HANDLE occlusionHandle =
        GetCullOcclusionHandle();
    if (cmd == nullptr || heap == nullptr || occlusionHandle.ptr == 0) {
        return false;
    }
    if (!RegisterGpuLodCullStateRollback(cullBuffer)) {
        return false;
    }

    MeshGpuLodCullConstants cullConstants{};
    BuildFrustumPlanes(camera.GetViewProjection(), cullConstants.frustumPlanes);
    const XMFLOAT3 cameraPosition = camera.GetPosition();
    const float safeMaxDistance =
        std::isfinite(maxDistance) ? (std::max)(maxDistance, 0.0f) : 0.0f;
    cullConstants.cameraAndMaxDistanceSq = {
        cameraPosition.x, cameraPosition.y, cameraPosition.z,
        safeMaxDistance * safeMaxDistance};
    const float radius =
        std::isfinite(localBounds.radius)
            ? (std::max)(localBounds.radius, 0.0001f)
            : 0.0001f;
    cullConstants.localCenterAndRadius = {
        std::isfinite(localBounds.center.x) ? localBounds.center.x : 0.0f,
        std::isfinite(localBounds.center.y) ? localBounds.center.y : 0.0f,
        std::isfinite(localBounds.center.z) ? localBounds.center.z : 0.0f,
        radius};
    cullConstants.lodOriginAndBias = {
        cameraPosition.x, cameraPosition.y, cameraPosition.z,
        static_cast<float>(lodBias)};
    cullConstants.lodDistanceBreaks = {
        std::isfinite(distanceBreaks[0]) ? distanceBreaks[0] : 0.0f,
        std::isfinite(distanceBreaks[1]) ? distanceBreaks[1] : 0.0f, 0.0f,
        0.0f};
    cullConstants.occlusionViewProjection = occlusionViewProjection_;
    cullConstants.occlusionParams =
        occlusionPyramidEnabled_ ? occlusionParams_
                                 : XMFLOAT4{0.0f, 0.0f, 0.0f, 0.006f};
    cullConstants.instanceCount = sourceInstances.instanceCount;
    cullConstants.enableDistanceCull = safeMaxDistance > 0.0f ? 1u : 0u;
    cullConstants.lodBias =
        (std::min)(lodBias, kMeshGpuCullLodCount - 1u);

    MeshGpuLodCullArgsConstants argsConstants{};
    argsConstants.indexCountPerInstance = {
        lodMeshes[0]->indexCount, lodMeshes[1]->indexCount,
        lodMeshes[2]->indexCount, 0u};
    argsConstants.maxInstanceCount = sourceInstances.instanceCount;
    argsConstants.startIndexLocation = 0u;
    argsConstants.baseVertexLocation = 0;
    argsConstants.startInstanceLocation = 0u;

    const D3D12_GPU_VIRTUAL_ADDRESS cullCb =
        uploadBuffer_.Write(cullConstants).gpu;
    const D3D12_GPU_VIRTUAL_ADDRESS argsCb =
        uploadBuffer_.Write(argsConstants).gpu;
    if (cullCb == 0 || argsCb == 0) {
        return false;
    }

    ID3D12DescriptorHeap *heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);

    std::vector<D3D12_RESOURCE_BARRIER> beforeCullBarriers;
    beforeCullBarriers.reserve(1u + kMeshGpuCullLodCount * 2u);
    beforeCullBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
        sourceInstances.resource.Get(),
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        if (cullBuffer.outputStates[lod] !=
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            beforeCullBarriers.push_back(
                CD3DX12_RESOURCE_BARRIER::Transition(
                    cullBuffer.outputResources[lod].Get(),
                    cullBuffer.outputStates[lod],
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
            cullBuffer.outputStates[lod] =
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (cullBuffer.drawArgsStates[lod] !=
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            beforeCullBarriers.push_back(
                CD3DX12_RESOURCE_BARRIER::Transition(
                    cullBuffer.drawArgsResources[lod].Get(),
                    cullBuffer.drawArgsStates[lod],
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
            cullBuffer.drawArgsStates[lod] =
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
    }
    cmd->ResourceBarrier(static_cast<UINT>(beforeCullBarriers.size()),
                         beforeCullBarriers.data());

    const UINT zeroValues[4] = {0u, 0u, 0u, 0u};
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        cmd->ClearUnorderedAccessViewUint(
            cullBuffer.countUavGpuHandles[lod],
            cullBuffer.countUavCpuHandles[lod],
            cullBuffer.countResources[lod].Get(), zeroValues, 0, nullptr);
    }
    std::array<D3D12_RESOURCE_BARRIER, kMeshGpuCullLodCount>
        countClearBarriers{};
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        countClearBarriers[lod] =
            CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.countResources[lod].Get());
    }
    cmd->ResourceBarrier(static_cast<UINT>(countClearBarriers.size()),
                         countClearBarriers.data());

    cmd->SetComputeRootSignature(gpuLodCullRootSignature_.Get());
    cmd->SetPipelineState(gpuLodCullPSO_.Get());
    cmd->SetComputeRootConstantBufferView(0, cullCb);
    cmd->SetComputeRootDescriptorTable(1, cullBuffer.sourceSrvGpuHandle);
    cmd->SetComputeRootDescriptorTable(2, occlusionHandle);
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        cmd->SetComputeRootDescriptorTable(
            3 + lod, cullBuffer.outputUavGpuHandles[lod]);
        cmd->SetComputeRootDescriptorTable(
            6 + lod, cullBuffer.countUavGpuHandles[lod]);
        cmd->SetComputeRootDescriptorTable(
            9 + lod, cullBuffer.drawArgsUavGpuHandles[lod]);
    }
    cmd->Dispatch((sourceInstances.instanceCount + 127u) / 128u, 1u, 1u);

    std::vector<D3D12_RESOURCE_BARRIER> countBarriers;
    countBarriers.reserve(kMeshGpuCullLodCount * 2u);
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        countBarriers.push_back(
            CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.outputResources[lod].Get()));
        countBarriers.push_back(
            CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.countResources[lod].Get()));
    }
    cmd->ResourceBarrier(static_cast<UINT>(countBarriers.size()),
                         countBarriers.data());

    cmd->SetPipelineState(gpuLodCullArgsPSO_.Get());
    cmd->SetComputeRootConstantBufferView(0, argsCb);
    cmd->Dispatch(1u, 1u, 1u);

    std::array<D3D12_RESOURCE_BARRIER, kMeshGpuCullLodCount> drawArgsUav{};
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        drawArgsUav[lod] =
            CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.drawArgsResources[lod].Get());
    }
    cmd->ResourceBarrier(static_cast<UINT>(drawArgsUav.size()),
                         drawArgsUav.data());

    std::vector<D3D12_RESOURCE_BARRIER> drawBarriers;
    drawBarriers.reserve(1u + kMeshGpuCullLodCount * 2u);
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        drawBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            cullBuffer.outputResources[lod].Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
        drawBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            cullBuffer.drawArgsResources[lod].Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT));
        cullBuffer.outputStates[lod] =
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        cullBuffer.drawArgsStates[lod] =
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }
    drawBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
        sourceInstances.resource.Get(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
    cmd->ResourceBarrier(static_cast<UINT>(drawBarriers.size()),
                         drawBarriers.data());

    InvalidateCommandState();
    const Material drawMaterial = NormalizeMaterialForDraw(material);
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(),
                             XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
        WriteMaterialConstants(drawMaterial);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || materialCbAddr == 0) {
        return false;
    }

    SetGraphicsRootSignatureCached(rootSignature_.Get());
    if (!SetInstancedPipelineForMaterial(
            customInstancedPipelines_[pipelineId].pipelineStates,
            drawMaterial)) {
        return false;
    }
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    SetGraphicsRootDescriptorTableCached(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));
    SetGraphicsRootDescriptorTableCached(4, shadowMapGpuHandle_);
    SetGraphicsRootDescriptorTableCached(
        5, textureManager_->GetGpuHandle(
               ResolveNormalTextureId(textureManager_, drawMaterial,
                                      normalTextureId)));

    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        if (drawIndex_ >= kMaxDraws) {
            return true;
        }
        D3D12_VERTEX_BUFFER_VIEW views[] = {lodMeshes[lod]->vbView,
                                            cullBuffer.outputViews[lod]};
        IASetVertexBuffersCached(0, 2, views);
        IASetIndexBufferCached(lodMeshes[lod]->ibView);
        IASetPrimitiveTopologyCached(lodMeshes[lod]->primitiveTopology);
        cmd->ExecuteIndirect(gpuCullCommandSignature_.Get(), 1,
                             cullBuffer.drawArgsResources[lod].Get(), 0,
                             nullptr, 0);
        ++drawIndex_;
    }
    return true;
}

bool MeshRenderer::DrawMeshInstancedGpuCulledShadowWithPipeline(
    uint32_t pipelineId, const Mesh &mesh, const Material &material,
    const MeshInstanceBuffer &sourceInstances, MeshGpuCullBuffer &cullBuffer,
    const MeshGpuCullBounds &localBounds,
    const DirectX::XMFLOAT4X4 &lightViewProjection, uint32_t textureId) {
    if (!dxCommon_ || !textureManager_ || !srvManager_ ||
        !shadowRootSignature_ || !gpuCullRootSignature_ || !gpuCullPSO_ ||
        !gpuCullArgsPSO_ || !gpuCullCommandSignature_ ||
        pipelineId >= customInstancedPipelines_.size() ||
        !customInstancedPipelines_[pipelineId].shadowPipelineState ||
        !IsDrawableMesh(mesh) || !sourceInstances.IsValid() ||
        drawIndex_ >= kMaxDraws) {
        return false;
    }
    if (!EnsureGpuCullBuffer(sourceInstances, cullBuffer)) {
        return false;
    }

    auto *cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    const D3D12_GPU_DESCRIPTOR_HANDLE occlusionHandle =
        GetCullOcclusionHandle();
    if (cmd == nullptr || heap == nullptr || occlusionHandle.ptr == 0) {
        return false;
    }
    if (!RegisterGpuCullStateRollback(cullBuffer)) {
        return false;
    }

    MeshGpuCullConstants cullConstants{};
    BuildFrustumPlanes(XMLoadFloat4x4(&lightViewProjection),
                       cullConstants.frustumPlanes);
    cullConstants.cameraAndMaxDistanceSq = {};
    const float radius =
        std::isfinite(localBounds.radius)
            ? (std::max)(localBounds.radius, 0.0001f)
            : 0.0001f;
    cullConstants.localCenterAndRadius = {
        std::isfinite(localBounds.center.x) ? localBounds.center.x : 0.0f,
        std::isfinite(localBounds.center.y) ? localBounds.center.y : 0.0f,
        std::isfinite(localBounds.center.z) ? localBounds.center.z : 0.0f,
        radius};
    cullConstants.occlusionViewProjection = occlusionViewProjection_;
    cullConstants.occlusionParams = {0.0f, 0.0f, 0.0f, 0.006f};
    cullConstants.instanceCount = sourceInstances.instanceCount;
    cullConstants.enableDistanceCull = 0u;

    MeshGpuCullArgsConstants argsConstants{};
    argsConstants.indexCountPerInstance = mesh.indexCount;
    argsConstants.maxInstanceCount = sourceInstances.instanceCount;
    argsConstants.startIndexLocation = 0u;
    argsConstants.baseVertexLocation = 0;
    argsConstants.startInstanceLocation = 0u;

    const D3D12_GPU_VIRTUAL_ADDRESS cullCb =
        uploadBuffer_.Write(cullConstants).gpu;
    const D3D12_GPU_VIRTUAL_ADDRESS argsCb =
        uploadBuffer_.Write(argsConstants).gpu;
    if (cullCb == 0 || argsCb == 0) {
        return false;
    }

    ID3D12DescriptorHeap *heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);

    std::vector<D3D12_RESOURCE_BARRIER> beforeCullBarriers;
    beforeCullBarriers.reserve(3);
    beforeCullBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
        sourceInstances.resource.Get(),
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    if (cullBuffer.outputState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        beforeCullBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            cullBuffer.outputResource.Get(), cullBuffer.outputState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        cullBuffer.outputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (cullBuffer.drawArgsState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        beforeCullBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            cullBuffer.drawArgsResource.Get(), cullBuffer.drawArgsState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        cullBuffer.drawArgsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    cmd->ResourceBarrier(static_cast<UINT>(beforeCullBarriers.size()),
                         beforeCullBarriers.data());

    const UINT zeroValues[4] = {0u, 0u, 0u, 0u};
    cmd->ClearUnorderedAccessViewUint(
        cullBuffer.countUavGpuHandle, cullBuffer.countUavCpuHandle,
        cullBuffer.countResource.Get(), zeroValues, 0, nullptr);
    D3D12_RESOURCE_BARRIER countClearBarrier =
        CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.countResource.Get());
    cmd->ResourceBarrier(1, &countClearBarrier);

    cmd->SetComputeRootSignature(gpuCullRootSignature_.Get());
    cmd->SetPipelineState(gpuCullPSO_.Get());
    cmd->SetComputeRootConstantBufferView(0, cullCb);
    cmd->SetComputeRootDescriptorTable(1, cullBuffer.sourceSrvGpuHandle);
    cmd->SetComputeRootDescriptorTable(2, occlusionHandle);
    cmd->SetComputeRootDescriptorTable(3, cullBuffer.outputUavGpuHandle);
    cmd->SetComputeRootDescriptorTable(4, cullBuffer.countUavGpuHandle);
    cmd->SetComputeRootDescriptorTable(5, cullBuffer.drawArgsUavGpuHandle);
    cmd->Dispatch((sourceInstances.instanceCount + 127u) / 128u, 1u, 1u);

    D3D12_RESOURCE_BARRIER cullUavBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.outputResource.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.countResource.Get())};
    cmd->ResourceBarrier(_countof(cullUavBarriers), cullUavBarriers);

    cmd->SetPipelineState(gpuCullArgsPSO_.Get());
    cmd->SetComputeRootConstantBufferView(0, argsCb);
    cmd->Dispatch(1u, 1u, 1u);

    D3D12_RESOURCE_BARRIER drawArgsUav =
        CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.drawArgsResource.Get());
    cmd->ResourceBarrier(1, &drawArgsUav);

    D3D12_RESOURCE_BARRIER drawBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            cullBuffer.outputResource.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        CD3DX12_RESOURCE_BARRIER::Transition(
            cullBuffer.drawArgsResource.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
        CD3DX12_RESOURCE_BARRIER::Transition(
            sourceInstances.resource.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)};
    cmd->ResourceBarrier(_countof(drawBarriers), drawBarriers);
    cullBuffer.outputState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    cullBuffer.drawArgsState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

    InvalidateCommandState();
    const Material drawMaterial = NormalizeMaterialForDraw(material);
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

    SetGraphicsRootSignatureCached(shadowRootSignature_.Get());
    SetPipelineStateCached(
        customInstancedPipelines_[pipelineId].shadowPipelineState.Get());
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    SetGraphicsRootDescriptorTableCached(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));
    D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, cullBuffer.outputView};
    IASetVertexBuffersCached(0, 2, views);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    cmd->ExecuteIndirect(gpuCullCommandSignature_.Get(), 1,
                         cullBuffer.drawArgsResource.Get(), 0, nullptr, 0);
    ++drawIndex_;
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
    uint32_t lodBias, uint32_t textureId) {
    if (!dxCommon_ || !textureManager_ || !srvManager_ ||
        !shadowRootSignature_ || !gpuLodCullRootSignature_ ||
        !gpuLodCullPSO_ || !gpuLodCullArgsPSO_ ||
        !gpuCullCommandSignature_ ||
        pipelineId >= customInstancedPipelines_.size() ||
        !customInstancedPipelines_[pipelineId].shadowPipelineState ||
        !sourceInstances.IsValid() || drawIndex_ >= kMaxDraws) {
        return false;
    }
    for (const Mesh *mesh : lodMeshes) {
        if (mesh == nullptr || !IsDrawableMesh(*mesh)) {
            return false;
        }
    }
    if (!EnsureGpuLodCullBuffer(sourceInstances, cullBuffer)) {
        return false;
    }

    auto *cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    const D3D12_GPU_DESCRIPTOR_HANDLE occlusionHandle =
        GetCullOcclusionHandle();
    if (cmd == nullptr || heap == nullptr || occlusionHandle.ptr == 0) {
        return false;
    }
    if (!RegisterGpuLodCullStateRollback(cullBuffer)) {
        return false;
    }

    MeshGpuLodCullConstants cullConstants{};
    BuildFrustumPlanes(XMLoadFloat4x4(&lightViewProjection),
                       cullConstants.frustumPlanes);
    cullConstants.cameraAndMaxDistanceSq = {};
    const float radius =
        std::isfinite(localBounds.radius)
            ? (std::max)(localBounds.radius, 0.0001f)
            : 0.0001f;
    cullConstants.localCenterAndRadius = {
        std::isfinite(localBounds.center.x) ? localBounds.center.x : 0.0f,
        std::isfinite(localBounds.center.y) ? localBounds.center.y : 0.0f,
        std::isfinite(localBounds.center.z) ? localBounds.center.z : 0.0f,
        radius};
    cullConstants.lodOriginAndBias = {
        std::isfinite(lodOrigin.x) ? lodOrigin.x : 0.0f,
        std::isfinite(lodOrigin.y) ? lodOrigin.y : 0.0f,
        std::isfinite(lodOrigin.z) ? lodOrigin.z : 0.0f,
        static_cast<float>(lodBias)};
    cullConstants.lodDistanceBreaks = {
        std::isfinite(distanceBreaks[0]) ? distanceBreaks[0] : 0.0f,
        std::isfinite(distanceBreaks[1]) ? distanceBreaks[1] : 0.0f, 0.0f,
        0.0f};
    cullConstants.occlusionViewProjection = occlusionViewProjection_;
    cullConstants.occlusionParams = {0.0f, 0.0f, 0.0f, 0.006f};
    cullConstants.instanceCount = sourceInstances.instanceCount;
    cullConstants.enableDistanceCull = 0u;
    cullConstants.lodBias =
        (std::min)(lodBias, kMeshGpuCullLodCount - 1u);

    MeshGpuLodCullArgsConstants argsConstants{};
    argsConstants.indexCountPerInstance = {
        lodMeshes[0]->indexCount, lodMeshes[1]->indexCount,
        lodMeshes[2]->indexCount, 0u};
    argsConstants.maxInstanceCount = sourceInstances.instanceCount;
    argsConstants.startIndexLocation = 0u;
    argsConstants.baseVertexLocation = 0;
    argsConstants.startInstanceLocation = 0u;

    const D3D12_GPU_VIRTUAL_ADDRESS cullCb =
        uploadBuffer_.Write(cullConstants).gpu;
    const D3D12_GPU_VIRTUAL_ADDRESS argsCb =
        uploadBuffer_.Write(argsConstants).gpu;
    if (cullCb == 0 || argsCb == 0) {
        return false;
    }

    ID3D12DescriptorHeap *heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);

    std::vector<D3D12_RESOURCE_BARRIER> beforeCullBarriers;
    beforeCullBarriers.reserve(1u + kMeshGpuCullLodCount * 2u);
    beforeCullBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
        sourceInstances.resource.Get(),
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        if (cullBuffer.outputStates[lod] !=
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            beforeCullBarriers.push_back(
                CD3DX12_RESOURCE_BARRIER::Transition(
                    cullBuffer.outputResources[lod].Get(),
                    cullBuffer.outputStates[lod],
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
            cullBuffer.outputStates[lod] =
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (cullBuffer.drawArgsStates[lod] !=
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            beforeCullBarriers.push_back(
                CD3DX12_RESOURCE_BARRIER::Transition(
                    cullBuffer.drawArgsResources[lod].Get(),
                    cullBuffer.drawArgsStates[lod],
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
            cullBuffer.drawArgsStates[lod] =
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
    }
    cmd->ResourceBarrier(static_cast<UINT>(beforeCullBarriers.size()),
                         beforeCullBarriers.data());

    const UINT zeroValues[4] = {0u, 0u, 0u, 0u};
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        cmd->ClearUnorderedAccessViewUint(
            cullBuffer.countUavGpuHandles[lod],
            cullBuffer.countUavCpuHandles[lod],
            cullBuffer.countResources[lod].Get(), zeroValues, 0, nullptr);
    }
    std::array<D3D12_RESOURCE_BARRIER, kMeshGpuCullLodCount>
        countClearBarriers{};
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        countClearBarriers[lod] =
            CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.countResources[lod].Get());
    }
    cmd->ResourceBarrier(static_cast<UINT>(countClearBarriers.size()),
                         countClearBarriers.data());

    cmd->SetComputeRootSignature(gpuLodCullRootSignature_.Get());
    cmd->SetPipelineState(gpuLodCullPSO_.Get());
    cmd->SetComputeRootConstantBufferView(0, cullCb);
    cmd->SetComputeRootDescriptorTable(1, cullBuffer.sourceSrvGpuHandle);
    cmd->SetComputeRootDescriptorTable(2, occlusionHandle);
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        cmd->SetComputeRootDescriptorTable(
            3 + lod, cullBuffer.outputUavGpuHandles[lod]);
        cmd->SetComputeRootDescriptorTable(
            6 + lod, cullBuffer.countUavGpuHandles[lod]);
        cmd->SetComputeRootDescriptorTable(
            9 + lod, cullBuffer.drawArgsUavGpuHandles[lod]);
    }
    cmd->Dispatch((sourceInstances.instanceCount + 127u) / 128u, 1u, 1u);

    std::vector<D3D12_RESOURCE_BARRIER> countBarriers;
    countBarriers.reserve(kMeshGpuCullLodCount * 2u);
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        countBarriers.push_back(
            CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.outputResources[lod].Get()));
        countBarriers.push_back(
            CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.countResources[lod].Get()));
    }
    cmd->ResourceBarrier(static_cast<UINT>(countBarriers.size()),
                         countBarriers.data());

    cmd->SetPipelineState(gpuLodCullArgsPSO_.Get());
    cmd->SetComputeRootConstantBufferView(0, argsCb);
    cmd->Dispatch(1u, 1u, 1u);

    std::array<D3D12_RESOURCE_BARRIER, kMeshGpuCullLodCount> drawArgsUav{};
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        drawArgsUav[lod] =
            CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.drawArgsResources[lod].Get());
    }
    cmd->ResourceBarrier(static_cast<UINT>(drawArgsUav.size()),
                         drawArgsUav.data());

    std::vector<D3D12_RESOURCE_BARRIER> drawBarriers;
    drawBarriers.reserve(1u + kMeshGpuCullLodCount * 2u);
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        drawBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            cullBuffer.outputResources[lod].Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
        drawBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            cullBuffer.drawArgsResources[lod].Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT));
        cullBuffer.outputStates[lod] =
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        cullBuffer.drawArgsStates[lod] =
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }
    drawBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
        sourceInstances.resource.Get(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
    cmd->ResourceBarrier(static_cast<UINT>(drawBarriers.size()),
                         drawBarriers.data());

    InvalidateCommandState();
    const Material drawMaterial = NormalizeMaterialForDraw(material);
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

    SetGraphicsRootSignatureCached(shadowRootSignature_.Get());
    SetPipelineStateCached(
        customInstancedPipelines_[pipelineId].shadowPipelineState.Get());
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    SetGraphicsRootDescriptorTableCached(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));

    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        if (drawIndex_ >= kMaxDraws) {
            return true;
        }
        D3D12_VERTEX_BUFFER_VIEW views[] = {lodMeshes[lod]->vbView,
                                            cullBuffer.outputViews[lod]};
        IASetVertexBuffersCached(0, 2, views);
        IASetIndexBufferCached(lodMeshes[lod]->ibView);
        IASetPrimitiveTopologyCached(lodMeshes[lod]->primitiveTopology);
        cmd->ExecuteIndirect(gpuCullCommandSignature_.Get(), 1,
                             cullBuffer.drawArgsResources[lod].Get(), 0,
                             nullptr, 0);
        ++drawIndex_;
    }
    return true;
}

bool MeshRenderer::RegisterGpuCullStateRollback(MeshGpuCullBuffer &buffer) {
    if (dxCommon_ == nullptr) {
        return false;
    }
    MeshGpuCullBuffer *target = &buffer;
    const D3D12_RESOURCE_STATES previousOutputState = buffer.outputState;
    const D3D12_RESOURCE_STATES previousDrawArgsState = buffer.drawArgsState;
    return dxCommon_->RegisterFrameRollback(
        target, [target, previousOutputState, previousDrawArgsState]() {
            target->outputState = previousOutputState;
            target->drawArgsState = previousDrawArgsState;
        });
}

bool MeshRenderer::RegisterGpuLodCullStateRollback(
    MeshGpuLodCullBuffer &buffer) {
    if (dxCommon_ == nullptr) {
        return false;
    }
    MeshGpuLodCullBuffer *target = &buffer;
    const auto previousOutputStates = buffer.outputStates;
    const auto previousDrawArgsStates = buffer.drawArgsStates;
    return dxCommon_->RegisterFrameRollback(
        target, [target, previousOutputStates, previousDrawArgsStates]() {
            target->outputStates = previousOutputStates;
            target->drawArgsStates = previousDrawArgsStates;
        });
}

D3D12_GPU_DESCRIPTOR_HANDLE MeshRenderer::GetCullOcclusionHandle() const {
    if (occlusionPyramidEnabled_ && occlusionPyramidGpuHandle_.ptr != 0) {
        return occlusionPyramidGpuHandle_;
    }
    return fallbackOcclusionGpuHandle_;
}
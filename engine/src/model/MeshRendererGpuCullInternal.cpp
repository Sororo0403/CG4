#include "internal/MeshRendererGpuCullInternal.h"

#include "../graphics/internal/FrustumPlaneUtils.h"
#include "graphics/DxHelpers.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX;

namespace MeshRendererGpuCullInternal {
void BuildFrustumPlanes(const XMMATRIX& viewProjection, XMFLOAT4 (&planes)[6]) {
    XMFLOAT4X4 m{};
    XMStoreFloat4x4(&m, viewProjection);
    planes[0] = FrustumPlaneUtils::NormalizePlane(
        XMVectorSet(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41));
    planes[1] = FrustumPlaneUtils::NormalizePlane(
        XMVectorSet(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41));
    planes[2] = FrustumPlaneUtils::NormalizePlane(
        XMVectorSet(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42));
    planes[3] = FrustumPlaneUtils::NormalizePlane(
        XMVectorSet(m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42));
    planes[4] = FrustumPlaneUtils::NormalizePlane(XMVectorSet(m._13, m._23, m._33, m._43));
    planes[5] = FrustumPlaneUtils::NormalizePlane(
        XMVectorSet(m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43));
}

XMFLOAT4 BuildCameraAndMaxDistanceSq(const XMFLOAT3& cameraPosition, float maxDistance) {
    const float safeMaxDistance = std::isfinite(maxDistance) ? (std::max)(maxDistance, 0.0f) : 0.0f;
    return {cameraPosition.x, cameraPosition.y, cameraPosition.z,
            safeMaxDistance * safeMaxDistance};
}

uint32_t IsDistanceCullEnabled(float maxDistance) {
    const float safeMaxDistance = std::isfinite(maxDistance) ? (std::max)(maxDistance, 0.0f) : 0.0f;
    return safeMaxDistance > 0.0f ? 1u : 0u;
}

uint32_t IsMinDistanceCullEnabled(float minDistance) {
    const float safeMinDistance = std::isfinite(minDistance) ? (std::max)(minDistance, 0.0f) : 0.0f;
    return safeMinDistance > 0.0f ? 1u : 0u;
}

XMFLOAT4 BuildLocalCenterAndRadius(const MeshGpuCullBounds& localBounds) {
    const float radius =
        std::isfinite(localBounds.radius) ? (std::max)(localBounds.radius, 0.0001f) : 0.0001f;
    return {std::isfinite(localBounds.center.x) ? localBounds.center.x : 0.0f,
            std::isfinite(localBounds.center.y) ? localBounds.center.y : 0.0f,
            std::isfinite(localBounds.center.z) ? localBounds.center.z : 0.0f, radius};
}

XMFLOAT4 BuildLodDistanceBreaks(
    const std::array<float, kMeshGpuCullLodCount - 1u>& distanceBreaks) {
    return {std::isfinite(distanceBreaks[0]) ? distanceBreaks[0] : 0.0f,
            std::isfinite(distanceBreaks[1]) ? distanceBreaks[1] : 0.0f, 0.0f, 0.0f};
}

MeshGpuCullArgsConstants BuildSingleCullArgs(const Mesh& mesh, uint32_t instanceCount) {
    MeshGpuCullArgsConstants argsConstants{};
    argsConstants.indexCountPerInstance = mesh.indexCount;
    argsConstants.maxInstanceCount = instanceCount;
    return argsConstants;
}

MeshGpuLodCullArgsConstants BuildLodCullArgs(
    const std::array<const Mesh*, kMeshGpuCullLodCount>& lodMeshes, uint32_t instanceCount) {
    MeshGpuLodCullArgsConstants argsConstants{};
    argsConstants.indexCountPerInstance = {lodMeshes[0]->indexCount, lodMeshes[1]->indexCount,
                                           lodMeshes[2]->indexCount, 0u};
    argsConstants.maxInstanceCount = instanceCount;
    return argsConstants;
}

void ExecuteSingleGpuCull(const SingleGpuCullExecutionContext& context) {
    ID3D12GraphicsCommandList* cmd = context.cmd;
    ID3D12DescriptorHeap* heap = context.heap;
    const MeshInstanceBuffer& sourceInstances = *context.sourceInstances;
    MeshGpuCullBuffer& cullBuffer = *context.cullBuffer;
    const UINT zeroValues[4] = {0u, 0u, 0u, 0u};
    ID3D12DescriptorHeap* heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);

    D3D12_RESOURCE_BARRIER sourceToShaderResource = CD3DX12_RESOURCE_BARRIER::Transition(
        sourceInstances.resource.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmd->ResourceBarrier(1, &sourceToShaderResource);
    if (cullBuffer.outputState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER outputToUav = CD3DX12_RESOURCE_BARRIER::Transition(
            cullBuffer.outputResource.Get(), cullBuffer.outputState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd->ResourceBarrier(1, &outputToUav);
        cullBuffer.outputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (cullBuffer.drawArgsState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER drawArgsToUav = CD3DX12_RESOURCE_BARRIER::Transition(
            cullBuffer.drawArgsResource.Get(), cullBuffer.drawArgsState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd->ResourceBarrier(1, &drawArgsToUav);
        cullBuffer.drawArgsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    cmd->ClearUnorderedAccessViewUint(cullBuffer.countUavGpuHandle, cullBuffer.countUavCpuHandle,
                                      cullBuffer.countResource.Get(), zeroValues, 0, nullptr);
    D3D12_RESOURCE_BARRIER countClearBarrier =
        CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.countResource.Get());
    cmd->ResourceBarrier(1, &countClearBarrier);

    cmd->SetComputeRootSignature(context.rootSignature);
    cmd->SetPipelineState(context.cullPSO);
    cmd->SetComputeRootConstantBufferView(0, context.cullCb);
    cmd->SetComputeRootDescriptorTable(1, cullBuffer.sourceSrvGpuHandle);
    cmd->SetComputeRootDescriptorTable(2, context.occlusionHandle);
    cmd->SetComputeRootDescriptorTable(3, cullBuffer.outputUavGpuHandle);
    cmd->SetComputeRootDescriptorTable(4, cullBuffer.countUavGpuHandle);
    cmd->SetComputeRootDescriptorTable(5, cullBuffer.drawArgsUavGpuHandle);
    cmd->Dispatch((sourceInstances.instanceCount + 127u) / 128u, 1u, 1u);

    D3D12_RESOURCE_BARRIER cullUavBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.outputResource.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.countResource.Get())};
    cmd->ResourceBarrier(_countof(cullUavBarriers), cullUavBarriers);

    cmd->SetPipelineState(context.argsPSO);
    cmd->SetComputeRootConstantBufferView(0, context.argsCb);
    cmd->Dispatch(1u, 1u, 1u);

    D3D12_RESOURCE_BARRIER drawArgsUav =
        CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.drawArgsResource.Get());
    cmd->ResourceBarrier(1, &drawArgsUav);

    D3D12_RESOURCE_BARRIER drawBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(cullBuffer.outputResource.Get(),
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                             D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        CD3DX12_RESOURCE_BARRIER::Transition(cullBuffer.drawArgsResource.Get(),
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                             D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
        CD3DX12_RESOURCE_BARRIER::Transition(sourceInstances.resource.Get(),
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                             D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)};
    cmd->ResourceBarrier(_countof(drawBarriers), drawBarriers);
    cullBuffer.outputState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    cullBuffer.drawArgsState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
}

void ExecuteLodGpuCull(const LodGpuCullExecutionContext& context) {
    ID3D12GraphicsCommandList* cmd = context.cmd;
    ID3D12DescriptorHeap* heap = context.heap;
    const MeshInstanceBuffer& sourceInstances = *context.sourceInstances;
    MeshGpuLodCullBuffer& cullBuffer = *context.cullBuffer;
    ID3D12DescriptorHeap* heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);

    D3D12_RESOURCE_BARRIER sourceToShaderResource = CD3DX12_RESOURCE_BARRIER::Transition(
        sourceInstances.resource.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmd->ResourceBarrier(1, &sourceToShaderResource);
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        if (cullBuffer.outputStates[lod] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            D3D12_RESOURCE_BARRIER outputToUav = CD3DX12_RESOURCE_BARRIER::Transition(
                cullBuffer.outputResources[lod].Get(), cullBuffer.outputStates[lod],
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cmd->ResourceBarrier(1, &outputToUav);
            cullBuffer.outputStates[lod] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (cullBuffer.drawArgsStates[lod] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            D3D12_RESOURCE_BARRIER drawArgsToUav = CD3DX12_RESOURCE_BARRIER::Transition(
                cullBuffer.drawArgsResources[lod].Get(), cullBuffer.drawArgsStates[lod],
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cmd->ResourceBarrier(1, &drawArgsToUav);
            cullBuffer.drawArgsStates[lod] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
    }

    const UINT zeroValues[4] = {0u, 0u, 0u, 0u};
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        cmd->ClearUnorderedAccessViewUint(
            cullBuffer.countUavGpuHandles[lod], cullBuffer.countUavCpuHandles[lod],
            cullBuffer.countResources[lod].Get(), zeroValues, 0, nullptr);
    }
    std::array<D3D12_RESOURCE_BARRIER, kMeshGpuCullLodCount> countClearBarriers{};
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        countClearBarriers[lod] =
            CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.countResources[lod].Get());
    }
    cmd->ResourceBarrier(static_cast<UINT>(countClearBarriers.size()), countClearBarriers.data());

    cmd->SetComputeRootSignature(context.rootSignature);
    cmd->SetPipelineState(context.cullPSO);
    cmd->SetComputeRootConstantBufferView(0, context.cullCb);
    cmd->SetComputeRootDescriptorTable(1, cullBuffer.sourceSrvGpuHandle);
    cmd->SetComputeRootDescriptorTable(2, context.occlusionHandle);
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        cmd->SetComputeRootDescriptorTable(3 + lod, cullBuffer.outputUavGpuHandles[lod]);
        cmd->SetComputeRootDescriptorTable(6 + lod, cullBuffer.countUavGpuHandles[lod]);
        cmd->SetComputeRootDescriptorTable(9 + lod, cullBuffer.drawArgsUavGpuHandles[lod]);
    }
    cmd->Dispatch((sourceInstances.instanceCount + 127u) / 128u, 1u, 1u);

    std::array<D3D12_RESOURCE_BARRIER, kMeshGpuCullLodCount * 2u> countBarriers{};
    UINT countBarrierCount = 0;
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        if (countBarrierCount >= countBarriers.size()) {
            return;
        }
        countBarriers[countBarrierCount++] =
            CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.outputResources[lod].Get());
        if (countBarrierCount >= countBarriers.size()) {
            return;
        }
        countBarriers[countBarrierCount++] =
            CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.countResources[lod].Get());
    }
    cmd->ResourceBarrier(countBarrierCount, countBarriers.data());

    cmd->SetPipelineState(context.argsPSO);
    cmd->SetComputeRootConstantBufferView(0, context.argsCb);
    cmd->Dispatch(1u, 1u, 1u);

    std::array<D3D12_RESOURCE_BARRIER, kMeshGpuCullLodCount> drawArgsUav{};
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        drawArgsUav[lod] = CD3DX12_RESOURCE_BARRIER::UAV(cullBuffer.drawArgsResources[lod].Get());
    }
    cmd->ResourceBarrier(static_cast<UINT>(drawArgsUav.size()), drawArgsUav.data());

    std::array<D3D12_RESOURCE_BARRIER, 1u + kMeshGpuCullLodCount * 2u> drawBarriers{};
    UINT drawBarrierCount = 0;
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        if (drawBarrierCount >= drawBarriers.size()) {
            return;
        }
        drawBarriers[drawBarrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(
            cullBuffer.outputResources[lod].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        if (drawBarrierCount >= drawBarriers.size()) {
            return;
        }
        drawBarriers[drawBarrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(
            cullBuffer.drawArgsResources[lod].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        cullBuffer.outputStates[lod] = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        cullBuffer.drawArgsStates[lod] = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }
    if (drawBarrierCount >= drawBarriers.size()) {
        return;
    }
    drawBarriers[drawBarrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(
        sourceInstances.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    cmd->ResourceBarrier(drawBarrierCount, drawBarriers.data());
}

} // namespace MeshRendererGpuCullInternal

#include "MeshRendererGpuCullInternal.h"
#include "graphics/DxHelpers.h"
#include "../graphics/FrustumPlaneUtils.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace DirectX;

namespace MeshRendererGpuCullInternal {
void BuildFrustumPlanes(const XMMATRIX &viewProjection,
                        XMFLOAT4 (&planes)[6]) {
    XMFLOAT4X4 m{};
    XMStoreFloat4x4(&m, viewProjection);
    planes[0] = FrustumPlaneUtils::NormalizePlane(
        XMVectorSet(m._14 + m._11, m._24 + m._21, m._34 + m._31,
                    m._44 + m._41));
    planes[1] = FrustumPlaneUtils::NormalizePlane(
        XMVectorSet(m._14 - m._11, m._24 - m._21, m._34 - m._31,
                    m._44 - m._41));
    planes[2] = FrustumPlaneUtils::NormalizePlane(
        XMVectorSet(m._14 - m._12, m._24 - m._22, m._34 - m._32,
                    m._44 - m._42));
    planes[3] = FrustumPlaneUtils::NormalizePlane(
        XMVectorSet(m._14 + m._12, m._24 + m._22, m._34 + m._32,
                    m._44 + m._42));
    planes[4] =
        FrustumPlaneUtils::NormalizePlane(XMVectorSet(m._13, m._23, m._33, m._43));
    planes[5] = FrustumPlaneUtils::NormalizePlane(
        XMVectorSet(m._14 - m._13, m._24 - m._23, m._34 - m._33,
                    m._44 - m._43));
}

XMFLOAT4 BuildCameraAndMaxDistanceSq(const XMFLOAT3 &cameraPosition,
                                     float maxDistance) {
    const float safeMaxDistance =
        std::isfinite(maxDistance) ? (std::max)(maxDistance, 0.0f) : 0.0f;
    return {cameraPosition.x, cameraPosition.y, cameraPosition.z,
            safeMaxDistance * safeMaxDistance};
}

uint32_t IsDistanceCullEnabled(float maxDistance) {
    const float safeMaxDistance =
        std::isfinite(maxDistance) ? (std::max)(maxDistance, 0.0f) : 0.0f;
    return safeMaxDistance > 0.0f ? 1u : 0u;
}

XMFLOAT4 BuildLocalCenterAndRadius(const MeshGpuCullBounds &localBounds) {
    const float radius =
        std::isfinite(localBounds.radius)
            ? (std::max)(localBounds.radius, 0.0001f)
            : 0.0001f;
    return {
        std::isfinite(localBounds.center.x) ? localBounds.center.x : 0.0f,
        std::isfinite(localBounds.center.y) ? localBounds.center.y : 0.0f,
        std::isfinite(localBounds.center.z) ? localBounds.center.z : 0.0f,
        radius};
}

XMFLOAT4 BuildLodDistanceBreaks(
    const std::array<float, kMeshGpuCullLodCount - 1u> &distanceBreaks) {
    return {std::isfinite(distanceBreaks[0]) ? distanceBreaks[0] : 0.0f,
            std::isfinite(distanceBreaks[1]) ? distanceBreaks[1] : 0.0f,
            0.0f, 0.0f};
}

MeshGpuCullArgsConstants BuildSingleCullArgs(const Mesh &mesh,
                                             uint32_t instanceCount) {
    MeshGpuCullArgsConstants argsConstants{};
    argsConstants.indexCountPerInstance = mesh.indexCount;
    argsConstants.maxInstanceCount = instanceCount;
    return argsConstants;
}

MeshGpuLodCullArgsConstants BuildLodCullArgs(
    const std::array<const Mesh *, kMeshGpuCullLodCount> &lodMeshes,
    uint32_t instanceCount) {
    MeshGpuLodCullArgsConstants argsConstants{};
    argsConstants.indexCountPerInstance = {
        lodMeshes[0]->indexCount, lodMeshes[1]->indexCount,
        lodMeshes[2]->indexCount, 0u};
    argsConstants.maxInstanceCount = instanceCount;
    return argsConstants;
}

void ExecuteSingleGpuCull(ID3D12GraphicsCommandList *cmd,
                          ID3D12DescriptorHeap *heap,
                          ID3D12RootSignature *rootSignature,
                          ID3D12PipelineState *cullPSO,
                          ID3D12PipelineState *argsPSO,
                          D3D12_GPU_DESCRIPTOR_HANDLE occlusionHandle,
                          const MeshInstanceBuffer &sourceInstances,
                          MeshGpuCullBuffer &cullBuffer,
                          D3D12_GPU_VIRTUAL_ADDRESS cullCb,
                          D3D12_GPU_VIRTUAL_ADDRESS argsCb) {
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

    cmd->SetComputeRootSignature(rootSignature);
    cmd->SetPipelineState(cullPSO);
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

    cmd->SetPipelineState(argsPSO);
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
}

void ExecuteLodGpuCull(ID3D12GraphicsCommandList *cmd,
                       ID3D12DescriptorHeap *heap,
                       ID3D12RootSignature *rootSignature,
                       ID3D12PipelineState *cullPSO,
                       ID3D12PipelineState *argsPSO,
                       D3D12_GPU_DESCRIPTOR_HANDLE occlusionHandle,
                       const MeshInstanceBuffer &sourceInstances,
                       MeshGpuLodCullBuffer &cullBuffer,
                       D3D12_GPU_VIRTUAL_ADDRESS cullCb,
                       D3D12_GPU_VIRTUAL_ADDRESS argsCb) {
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

    cmd->SetComputeRootSignature(rootSignature);
    cmd->SetPipelineState(cullPSO);
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
        countBarriers.push_back(CD3DX12_RESOURCE_BARRIER::UAV(
            cullBuffer.outputResources[lod].Get()));
        countBarriers.push_back(CD3DX12_RESOURCE_BARRIER::UAV(
            cullBuffer.countResources[lod].Get()));
    }
    cmd->ResourceBarrier(static_cast<UINT>(countBarriers.size()),
                         countBarriers.data());

    cmd->SetPipelineState(argsPSO);
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
}

} // namespace MeshRendererGpuCullInternal

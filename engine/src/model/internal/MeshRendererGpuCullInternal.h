#pragma once

#include "model/MeshGpuCullBuffer.h"
#include "model/MeshInstanceBuffer.h"
#include "model/MeshManager.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <d3d12.h>

namespace MeshRendererGpuCullInternal {

struct MeshGpuCullConstants {
    DirectX::XMFLOAT4 frustumPlanes[6];
    DirectX::XMFLOAT4 cameraAndMaxDistanceSq;
    DirectX::XMFLOAT4 localCenterAndRadius;
    DirectX::XMFLOAT4X4 occlusionViewProjection;
    DirectX::XMFLOAT4 occlusionParams;
    uint32_t instanceCount = 0;
    uint32_t enableDistanceCull = 0;
    float minDistanceSq = 0.0f;
    uint32_t enableMinDistanceCull = 0;
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
    DirectX::XMFLOAT4 frustumPlanes[6];
    DirectX::XMFLOAT4 cameraAndMaxDistanceSq;
    DirectX::XMFLOAT4 localCenterAndRadius;
    DirectX::XMFLOAT4 lodOriginAndBias;
    DirectX::XMFLOAT4 lodDistanceBreaks;
    DirectX::XMFLOAT4X4 occlusionViewProjection;
    DirectX::XMFLOAT4 occlusionParams;
    uint32_t instanceCount = 0;
    uint32_t enableDistanceCull = 0;
    uint32_t lodBias = 0;
    uint32_t paddingParam = 0;
};

struct MeshGpuLodCullArgsConstants {
    DirectX::XMUINT4 indexCountPerInstance{};
    uint32_t maxInstanceCount = 0;
    uint32_t startIndexLocation = 0;
    int32_t baseVertexLocation = 0;
    uint32_t startInstanceLocation = 0;
};

void BuildFrustumPlanes(const DirectX::XMMATRIX &viewProjection,
                        DirectX::XMFLOAT4 (&planes)[6]);
DirectX::XMFLOAT4 BuildCameraAndMaxDistanceSq(
    const DirectX::XMFLOAT3 &cameraPosition, float maxDistance);
uint32_t IsDistanceCullEnabled(float maxDistance);
uint32_t IsMinDistanceCullEnabled(float minDistance);
DirectX::XMFLOAT4
BuildLocalCenterAndRadius(const MeshGpuCullBounds &localBounds);
DirectX::XMFLOAT4 BuildLodDistanceBreaks(
    const std::array<float, kMeshGpuCullLodCount - 1u> &distanceBreaks);
MeshGpuCullArgsConstants BuildSingleCullArgs(const Mesh &mesh,
                                             uint32_t instanceCount);
MeshGpuLodCullArgsConstants BuildLodCullArgs(
    const std::array<const Mesh *, kMeshGpuCullLodCount> &lodMeshes,
    uint32_t instanceCount);

void ExecuteSingleGpuCull(ID3D12GraphicsCommandList *cmd,
                          ID3D12DescriptorHeap *heap,
                          ID3D12RootSignature *rootSignature,
                          ID3D12PipelineState *cullPSO,
                          ID3D12PipelineState *argsPSO,
                          D3D12_GPU_DESCRIPTOR_HANDLE occlusionHandle,
                          const MeshInstanceBuffer &sourceInstances,
                          MeshGpuCullBuffer &cullBuffer,
                          D3D12_GPU_VIRTUAL_ADDRESS cullCb,
                          D3D12_GPU_VIRTUAL_ADDRESS argsCb);
void ExecuteLodGpuCull(ID3D12GraphicsCommandList *cmd,
                       ID3D12DescriptorHeap *heap,
                       ID3D12RootSignature *rootSignature,
                       ID3D12PipelineState *cullPSO,
                       ID3D12PipelineState *argsPSO,
                       D3D12_GPU_DESCRIPTOR_HANDLE occlusionHandle,
                       const MeshInstanceBuffer &sourceInstances,
                       MeshGpuLodCullBuffer &cullBuffer,
                       D3D12_GPU_VIRTUAL_ADDRESS cullCb,
                       D3D12_GPU_VIRTUAL_ADDRESS argsCb);

} // namespace MeshRendererGpuCullInternal

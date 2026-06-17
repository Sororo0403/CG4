#pragma once

#include "core/ResourceHandle.h"
#include "graphics/DepthPyramid.h"

#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl.h>

struct DepthPyramid::State {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    std::vector<D3D12_RESOURCE_STATES> subresourceStates;
    uint32_t descriptorStart = kInvalidResourceId;
    uint32_t descriptorCount = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle{};
    uint32_t sourceWidth = 1;
    uint32_t sourceHeight = 1;
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t mipCount = 0;
};

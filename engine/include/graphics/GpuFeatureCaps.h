#pragma once

#include <d3d12.h>

struct GpuFeatureCaps {
    D3D12_RAYTRACING_TIER raytracingTier =
        D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    D3D12_MESH_SHADER_TIER meshShaderTier =
        D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
    bool raytracingSupported = false;
    bool meshShaderSupported = false;
};

inline const char *GpuRaytracingTierName(D3D12_RAYTRACING_TIER tier) {
    switch (tier) {
    case D3D12_RAYTRACING_TIER_1_0:
        return "DXR Tier 1.0";
    case D3D12_RAYTRACING_TIER_1_1:
        return "DXR Tier 1.1";
    case D3D12_RAYTRACING_TIER_NOT_SUPPORTED:
    default:
        return "DXR unsupported";
    }
}

inline const char *GpuMeshShaderTierName(D3D12_MESH_SHADER_TIER tier) {
    switch (tier) {
    case D3D12_MESH_SHADER_TIER_1:
        return "Mesh Shader Tier 1";
    case D3D12_MESH_SHADER_TIER_NOT_SUPPORTED:
    default:
        return "Mesh Shader unsupported";
    }
}

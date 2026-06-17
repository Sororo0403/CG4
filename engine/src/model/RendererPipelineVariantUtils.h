#pragma once

#include "model/Material.h"
#include "model/MeshPipelineFactory.h"

#include <cstddef>
#include <d3d12.h>

namespace RendererPipelineVariantUtils {

enum class PipelineBlendMode : size_t {
    Opaque = 0,
    Alpha = 1,
    Additive = 2,
};

inline D3D12_CULL_MODE ToD3D12CullMode(const MaterialCullMode mode) {
    switch (mode) {
    case MaterialCullMode::None:
        return D3D12_CULL_MODE_NONE;
    case MaterialCullMode::Front:
        return D3D12_CULL_MODE_FRONT;
    case MaterialCullMode::Back:
    default:
        return D3D12_CULL_MODE_BACK;
    }
}

inline D3D12_CULL_MODE ToD3D12CullMode(const MeshCullMode mode) {
    switch (mode) {
    case MeshCullMode::None:
        return D3D12_CULL_MODE_NONE;
    case MeshCullMode::Front:
        return D3D12_CULL_MODE_FRONT;
    case MeshCullMode::Back:
    default:
        return D3D12_CULL_MODE_BACK;
    }
}

inline size_t PipelineVariantIndex(PipelineBlendMode blendMode,
                                   MaterialCullMode cullMode,
                                   bool depthWrite) {
    const size_t blendIndex = static_cast<size_t>(blendMode);
    const size_t cullIndex = static_cast<size_t>(cullMode);
    const size_t depthIndex = depthWrite ? 1u : 0u;
    return blendIndex * 6u + cullIndex * 2u + depthIndex;
}

inline size_t MaterialPipelineVariantIndex(bool transparent,
                                           MaterialCullMode cullMode,
                                           bool depthWrite) {
    return PipelineVariantIndex(
        transparent ? PipelineBlendMode::Alpha : PipelineBlendMode::Opaque,
        cullMode, depthWrite);
}

} // namespace RendererPipelineVariantUtils

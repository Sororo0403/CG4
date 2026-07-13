#pragma once

#include "graphics/DxHelpers.h"
#include "model/Material.h"
#include "model/MeshPipelineFactory.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <d3d12.h>
#include <iterator>

namespace RendererPipelineVariantUtils {

enum class PipelineBlendMode : size_t {
    Opaque = 0,
    Alpha = 1,
    Additive = 2,
};

inline D3D12_CULL_MODE ToD3D12CullMode(const MaterialCullMode mode) {
    constexpr std::array<D3D12_CULL_MODE, 3> kCullModes = {
        D3D12_CULL_MODE_NONE, D3D12_CULL_MODE_FRONT, D3D12_CULL_MODE_BACK};
    const size_t index = static_cast<size_t>(mode);
    if (index < kCullModes.size()) {
        return kCullModes[index];
    }
    return D3D12_CULL_MODE_BACK;
}

inline D3D12_CULL_MODE ToD3D12CullMode(const MeshCullMode mode) {
    constexpr std::array<D3D12_CULL_MODE, 3> kCullModes = {
        D3D12_CULL_MODE_BACK, D3D12_CULL_MODE_FRONT, D3D12_CULL_MODE_NONE};
    const size_t index = static_cast<size_t>(mode);
    if (index < kCullModes.size()) {
        return kCullModes[index];
    }
    return D3D12_CULL_MODE_BACK;
}

inline size_t PipelineVariantIndex(PipelineBlendMode blendMode, MaterialCullMode cullMode,
                                   bool depthWrite) {
    const size_t blendIndex = static_cast<size_t>(blendMode);
    const size_t cullIndex = static_cast<size_t>(cullMode);
    const size_t depthIndex = depthWrite ? 1u : 0u;
    return blendIndex * 6u + cullIndex * 2u + depthIndex;
}

inline size_t MaterialPipelineVariantIndex(bool transparent, MaterialCullMode cullMode,
                                           bool depthWrite) {
    return PipelineVariantIndex(transparent ? PipelineBlendMode::Alpha : PipelineBlendMode::Opaque,
                                cullMode, depthWrite);
}

template <typename PipelineRange> inline bool HasAllPipelineStates(const PipelineRange& pipelines) {
    return std::all_of(std::begin(pipelines), std::end(pipelines),
                       [](const auto& pipeline) { return pipeline != nullptr; });
}

inline std::array<CD3DX12_STATIC_SAMPLER_DESC, 2> MakeMaterialTextureSamplers(
    D3D12_TEXTURE_ADDRESS_MODE linearAddressV) {
    std::array<CD3DX12_STATIC_SAMPLER_DESC, 2> samplers = {
        CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR),
        CD3DX12_STATIC_SAMPLER_DESC(1, D3D12_FILTER_MIN_MAG_MIP_POINT),
    };
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV = linearAddressV;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    return samplers;
}

} // namespace RendererPipelineVariantUtils

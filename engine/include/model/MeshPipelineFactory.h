#pragma once

#include "model/Material.h"

#include <array>
#include <cstddef>
#include <d3d12.h>
#include <string>
#include <wrl.h>

enum class MeshBlendMode {
    Opaque,
    Alpha,
    Additive,
};

enum class MeshDepthMode {
    TestWrite,
    TestOnly,
    None,
};

enum class MeshCullMode {
    Back,
    Front,
    None,
};

enum class MeshPipelineVariantMode {
    MaterialDriven,
    Fixed,
};

struct MeshPipelineDesc {
    std::wstring vertexShader;
    std::wstring pixelShader;
    MeshBlendMode blend = MeshBlendMode::Opaque;
    MeshDepthMode depth = MeshDepthMode::TestWrite;
    MeshCullMode cull = MeshCullMode::Back;
    bool instanced = false;
    MeshPipelineVariantMode variantMode = MeshPipelineVariantMode::MaterialDriven;
};

static constexpr size_t kMeshPipelineVariantCount = 12;

using MeshPipelineStateArray =
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kMeshPipelineVariantCount>;

struct MeshPipelineSet {
    MeshPipelineStateArray pipelineStates;
};

class MeshPipelineFactory {
public:
    static MeshPipelineSet CreatePipelineSet(ID3D12Device* device,
                                             ID3D12RootSignature* rootSignature,
                                             const MeshPipelineDesc& desc,
                                             D3D12_INPUT_LAYOUT_DESC inputLayout,
                                             DXGI_FORMAT renderTargetFormat,
                                             DXGI_FORMAT depthStencilFormat);
};

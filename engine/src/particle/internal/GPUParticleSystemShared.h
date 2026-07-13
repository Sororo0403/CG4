#pragma once

#include "particle/GPUParticleSystem.h"

#include <d3d12.h>
#include <string>

namespace GpuParticleShared {

ID3D12RootSignature* GetDrawRootSignature(ID3D12Device* device);
ID3D12CommandSignature* GetDrawCommandSignature(ID3D12Device* device);
ID3D12PipelineState* GetOrCreateDrawPipeline(ID3D12Device* device,
                                             ID3D12RootSignature* rootSignature,
                                             const std::wstring& pixelShaderPath,
                                             GPUParticleMaterialSettings::BlendMode blendMode);
void ReleaseDrawResources();

} // namespace GpuParticleShared

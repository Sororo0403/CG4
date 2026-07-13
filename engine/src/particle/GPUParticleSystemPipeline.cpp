#include "../graphics/internal/RootSignatureUtils.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "internal/GPUParticleSystemInternal.h"
#include "internal/GPUParticleSystemShared.h"
#include "particle/GPUParticleSystem.h"

void GPUParticleSystem::CreateRootSignatures() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    {
        static_assert((sizeof(EmitterForGPU) / sizeof(uint32_t)) + 2u + 7u <= 64u,
                      "GPUParticle update root signature exceeds 64 DWORDs");
        CD3DX12_ROOT_PARAMETER params[9]{};
        params[0].InitAsConstantBufferView(0);
        params[1].InitAsConstants(static_cast<UINT>(sizeof(EmitterForGPU) / sizeof(uint32_t)), 1);

        CD3DX12_DESCRIPTOR_RANGE particleRange{};
        particleRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        params[2].InitAsDescriptorTable(1, &particleRange);

        CD3DX12_DESCRIPTOR_RANGE freeListRange{};
        freeListRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
        params[3].InitAsDescriptorTable(1, &freeListRange);

        CD3DX12_DESCRIPTOR_RANGE freeListIndexRange{};
        freeListIndexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);
        params[4].InitAsDescriptorTable(1, &freeListIndexRange);

        CD3DX12_DESCRIPTOR_RANGE activeIndexRange{};
        activeIndexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3);
        params[5].InitAsDescriptorTable(1, &activeIndexRange);

        CD3DX12_DESCRIPTOR_RANGE activeCountRange{};
        activeCountRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 4);
        params[6].InitAsDescriptorTable(1, &activeCountRange);

        CD3DX12_DESCRIPTOR_RANGE drawArgsRange{};
        drawArgsRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 5);
        params[7].InitAsDescriptorTable(1, &drawArgsRange);

        CD3DX12_DESCRIPTOR_RANGE explicitSpawnRange{};
        explicitSpawnRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        params[8].InitAsDescriptorTable(1, &explicitSpawnRange);

        CD3DX12_ROOT_SIGNATURE_DESC desc;
        desc.Init(_countof(params), params, 0, nullptr);

        if (!RootSignatureUtils::CreateRootSignature(dxCommon_->GetDevice(), desc,
                                                     resources_->updateRootSignature)) {
            return;
        }
    }

    resources_->drawRootSignature = GpuParticleShared::GetDrawRootSignature(dxCommon_->GetDevice());
}

void GPUParticleSystem::CreatePipelineStates() {
    auto* device = dxCommon_->GetDevice();
    if (device == nullptr || !resources_->updateRootSignature || !resources_->drawRootSignature) {
        return;
    }

    auto cs = ShaderCompiler::Compile(ShaderPaths::ParticleUpdateCS, "main", "cs_6_6");
    if (!cs) {
        return;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC computePso{};
    computePso.pRootSignature = resources_->updateRootSignature.Get();
    computePso.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
    if (FAILED(device->CreateComputePipelineState(&computePso,
                                                  IID_PPV_ARGS(&resources_->updatePso))) ||
        !resources_->updatePso) {
        return;
    }

    resources_->drawCommandSignature = GpuParticleShared::GetDrawCommandSignature(device);

    const std::wstring pixelShaderPath = materialSettings_.pixelShaderPath.empty()
                                             ? std::wstring(ShaderPaths::ParticlePS)
                                             : materialSettings_.pixelShaderPath;
    resources_->drawPso = GpuParticleShared::GetOrCreateDrawPipeline(
        device, resources_->drawRootSignature.Get(), pixelShaderPath, materialSettings_.blendMode);
}

#pragma once
#include "MeshRendererCommandCache.h"
#include "RendererSharedState.h"
#include "graphics/UploadRingBuffer.h"
#include "model/ModelRenderer.h"

#include <array>
#include <memory>
#include <vector>
#include <wrl.h>

struct ModelRenderer::State : RendererShadowState {
    DirectXCommon* dxCommon = nullptr;
    SrvManager* srvManager = nullptr;
    MeshManager* meshManager = nullptr;
    TextureManager* textureManager = nullptr;
    MaterialManager* materialManager = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> shadowRootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> skinningRootSignature;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, ModelRenderer::kPipelineVariantCount>
        pipelineStates;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, ModelRenderer::kPipelineVariantCount>
        instancedPipelineStates;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> instancedShadowPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skinningPSO;

    UploadRingBuffer uploadBuffer;
    std::vector<SkinPaletteFrame> identityPaletteFrames;
    uint32_t drawIndex = 0;
    uint64_t skinningFrameId = 0;
    SceneLighting currentLighting{};
    SceneFog currentFog{};
    uint32_t environmentTextureId = 0;
    uint32_t dissolveNoiseTextureId = 0;
    ModelDrawEffect currentEffect{};
    ID3D12RootSignature* currentGraphicsRootSignature = nullptr;
    ID3D12PipelineState* currentGraphicsPipelineState = nullptr;
    std::unique_ptr<MeshRendererCommandCache> commandCache =
        std::make_unique<MeshRendererCommandCache>();
    std::vector<InstanceData> instanceScratch;
    bool hasEnvironmentTexture = false;
};

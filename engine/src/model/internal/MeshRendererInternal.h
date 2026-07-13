#pragma once
#include "MeshRendererCommandCache.h"
#include "RendererSharedState.h"
#include "graphics/UploadRingBuffer.h"
#include "model/MeshRenderer.h"

#include <array>
#include <vector>
#include <wrl.h>

struct MeshRenderer::InstancedPipelineSet {
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, MeshRenderer::kPipelineVariantCount>
        pipelineStates;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, MeshRenderer::kPipelineVariantCount>
        shadowPipelineStates;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, MeshRenderer::kPipelineVariantCount>
        opaqueShadowPipelineStates;
};

struct MeshRenderer::State : RendererShadowState {
    DirectXCommon* dxCommon = nullptr;
    SrvManager* srvManager = nullptr;
    TextureManager* textureManager = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> shadowRootSignature;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, MeshRenderer::kPipelineVariantCount>
        pipelineStates;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, MeshRenderer::kPipelineVariantCount>
        instancedPipelineStates;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> instancedShadowPSO;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> gpuCullRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuCullPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuCullArgsPSO;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> gpuCullCommandSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> gpuLodCullRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuLodCullPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuLodCullArgsPSO;
    Microsoft::WRL::ComPtr<ID3D12Resource> fallbackOcclusionTexture;
    uint32_t fallbackOcclusionSrvIndex = kInvalidResourceId;
    D3D12_GPU_DESCRIPTOR_HANDLE fallbackOcclusionGpuHandle{};

    std::vector<MeshPipelineSet> customPipelines;
    std::vector<InstancedPipelineSet> customInstancedPipelines;

    UploadRingBuffer uploadBuffer;
    std::vector<std::vector<MeshInstanceBuffer>> retiredStaticInstanceBuffers;
    uint32_t drawIndex = 0;
    std::unique_ptr<MeshRendererCommandCache> commandCache =
        std::make_unique<MeshRendererCommandCache>();
    ConstantCacheEntry sceneConstantsCache{};
    ConstantCacheEntry shadowSceneConstantsCache{};
    ConstantCacheEntry materialConstantsCache{};
    std::vector<InstanceData> instanceScratch;

    SceneLighting currentLighting{};
    SceneFog currentFog{};
    uint32_t environmentTextureId = kInvalidResourceId;
    bool materialReflectionsEnabled = true;
    DirectX::XMFLOAT4 customSceneParams0{1.0f, 0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 customSceneParams1{0.0f, 1.0f, 0.24f, 0.0f};
    DirectX::XMFLOAT4X4 occlusionViewProjection = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                                   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4 occlusionParams{0.0f, 0.0f, 0.0f, 0.006f};
    D3D12_GPU_DESCRIPTOR_HANDLE occlusionPyramidGpuHandle{};
    bool occlusionPyramidEnabled = false;
};

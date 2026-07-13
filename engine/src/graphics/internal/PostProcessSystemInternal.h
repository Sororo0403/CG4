#pragma once
#include "graphics/PostProcessSettings.h"

#include <array>
#include <d3d12.h>
#include <vector>
#include <wrl.h>

namespace PostProcessSystemInternal {

inline constexpr uint32_t kMaxBloomLevels = 5u;

struct BloomPassConstants {
    float sourceTexelSize[2]{};
    float targetTexelSize[2]{};
    float threshold = 1.0f;
    float softKnee = 0.55f;
    float radius = 1.0f;
    float intensity = 1.0f;
};

struct BloomLevel {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    uint32_t width = 1u;
    uint32_t height = 1u;
};

} // namespace PostProcessSystemInternal

struct PostProcessSystem::ConstantFrame {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    PostProcessConstants* mapped = nullptr;

    void Reset() {
        if (resource && mapped != nullptr) {
            resource->Unmap(0, nullptr);
            mapped = nullptr;
        }
        resource.Reset();
    }
};

struct PostProcessSystem::State {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> bloomRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> copyPipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> bloomExtractPipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> bloomDownsamplePipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> bloomUpsamplePipelineState;
    std::vector<ConstantFrame> constantFrames;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> bloomRtvHeap;
    std::array<PostProcessSystemInternal::BloomLevel, PostProcessSystemInternal::kMaxBloomLevels>
        bloomLevels;
    UINT bloomRtvDescriptorSize = 0;
    UINT bloomSrvStart = UINT_MAX;
    UINT bloomSrvCount = 0;
    uint32_t bloomLevelCount = 0u;
    PostProcessConstants constants{};
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissorRect{};
    PostProcessProfile profile{};
    int width = 1;
    int height = 1;
};

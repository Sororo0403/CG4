#pragma once
#include <d3d12.h>
#include <dxcapi.h>
#include <string>
#include <unordered_map>
#include <wrl.h>

struct PipelineManager::State {
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<IDxcBlob>> shaderCache;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> graphicsPipelines;
};

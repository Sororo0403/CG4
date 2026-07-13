#include "graphics/PipelineManager.h"

#include "graphics/DirectXCommon.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/ShaderCompiler.h"
#include "internal/PipelineManagerInternal.h"

#include <cstdint>
#include <exception>
#include <limits>
#include <new>

PipelineManager::PipelineManager() : state_(std::make_unique<State>()) {}

PipelineManager::~PipelineManager() {
    Clear(true);
}

void PipelineManager::Initialize(DirectXCommon* dxCommon) {
    if (!dxCommon) {
        if (Clear()) {
            dxCommon_ = nullptr;
        }
        return;
    }
    if (dxCommon_ != dxCommon) {
        if (!Clear()) {
            return;
        }
    }
    dxCommon_ = dxCommon;
}

IDxcBlob* PipelineManager::CompileShader(const std::wstring& path, const std::string& entry,
                                         const std::string& target) {
    std::string key;
    try {
        key = MakeShaderKey(path, entry, target);
    } catch (const std::exception&) {
        return nullptr;
    }
    auto it = state_->shaderCache.find(key);
    if (it != state_->shaderCache.end()) {
        return it->second.Get();
    }

    auto shader = ShaderCompiler::Compile(path, entry, target);
    if (!shader) {
        return nullptr;
    }
    IDxcBlob* result = shader.Get();
    try {
        state_->shaderCache.emplace(key, std::move(shader));
    } catch (const std::exception&) {
        return nullptr;
    }
    return result;
}

ID3D12PipelineState* PipelineManager::CreateGraphicsPipeline(
    const std::string& name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    if (FAILED(
            dxCommon_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline))) ||
        !pipeline) {
        return nullptr;
    }

    ID3D12PipelineState* result = pipeline.Get();
    try {
        state_->graphicsPipelines[name] = std::move(pipeline);
    } catch (const std::exception&) {
        return nullptr;
    }
    return result;
}

ID3D12PipelineState* PipelineManager::CreatePipelineStateStream(
    const std::string& name, const D3D12_PIPELINE_STATE_STREAM_DESC& desc) {
    if (!dxCommon_ || !dxCommon_->GetDevice() || desc.pPipelineStateSubobjectStream == nullptr ||
        desc.SizeInBytes == 0u) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<ID3D12Device2> device2;
    if (FAILED(dxCommon_->GetDevice()->QueryInterface(IID_PPV_ARGS(&device2))) || !device2) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    if (FAILED(device2->CreatePipelineState(&desc, IID_PPV_ARGS(&pipeline))) || !pipeline) {
        return nullptr;
    }

    ID3D12PipelineState* result = pipeline.Get();
    try {
        state_->graphicsPipelines[name] = std::move(pipeline);
    } catch (const std::exception&) {
        return nullptr;
    }
    return result;
}

ID3D12PipelineState* PipelineManager::GetGraphicsPipeline(const std::string& name) const {
    auto it = state_->graphicsPipelines.find(name);
    return it == state_->graphicsPipelines.end() ? nullptr : it->second.Get();
}

bool PipelineManager::Clear() {
    return Clear(false);
}

bool PipelineManager::Clear(bool allowFrameAbort) {
    if (!CanReleaseGpuResources(dxCommon_, !state_->graphicsPipelines.empty(), allowFrameAbort)) {
        return false;
    }

    state_->graphicsPipelines.clear();
    state_->shaderCache.clear();
    return true;
}

std::string PipelineManager::MakeShaderKey(const std::wstring& path, const std::string& entry,
                                           const std::string& target) {
    std::string pathKey;
    if (path.size() > (std::numeric_limits<size_t>::max)() / 6u) {
        return {};
    }
    pathKey.reserve(path.size() * 6u);
    for (wchar_t ch : path) {
        pathKey += std::to_string(static_cast<uint32_t>(ch));
        pathKey.push_back(',');
    }
    pathKey += "|";
    pathKey += entry;
    pathKey += "|";
    pathKey += target;
    return pathKey;
}

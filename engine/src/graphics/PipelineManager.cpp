#include "graphics/PipelineManager.h"
#include "graphics/DirectXCommon.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/ShaderCompiler.h"
#include <cstdint>

PipelineManager::~PipelineManager() {
    Clear(true);
}

void PipelineManager::Initialize(DirectXCommon *dxCommon) {
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

IDxcBlob *PipelineManager::CompileShader(const std::wstring &path,
                                         const std::string &entry,
                                         const std::string &target) {
    const std::string key = MakeShaderKey(path, entry, target);
    auto it = shaderCache_.find(key);
    if (it != shaderCache_.end()) {
        return it->second.Get();
    }

    auto shader = ShaderCompiler::Compile(path, entry, target);
    if (!shader) {
        return nullptr;
    }
    IDxcBlob *result = shader.Get();
    shaderCache_.emplace(key, std::move(shader));
    return result;
}

ID3D12PipelineState *PipelineManager::CreateGraphicsPipeline(
    const std::string &name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc) {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&pipeline))) ||
        !pipeline) {
        return nullptr;
    }

    ID3D12PipelineState *result = pipeline.Get();
    graphicsPipelines_[name] = std::move(pipeline);
    return result;
}

ID3D12PipelineState *
PipelineManager::GetGraphicsPipeline(const std::string &name) const {
    auto it = graphicsPipelines_.find(name);
    return it == graphicsPipelines_.end() ? nullptr : it->second.Get();
}

bool PipelineManager::Clear() { return Clear(false); }

bool PipelineManager::Clear(bool allowFrameAbort) {
    if (!CanReleaseGpuResources(dxCommon_, !graphicsPipelines_.empty(),
                                allowFrameAbort)) {
        return false;
    }

    graphicsPipelines_.clear();
    shaderCache_.clear();
    return true;
}

std::string PipelineManager::MakeShaderKey(const std::wstring &path,
                                           const std::string &entry,
                                           const std::string &target) {
    std::string pathKey;
    pathKey.reserve(path.size() * 6u);
    for (wchar_t ch : path) {
        pathKey += std::to_string(static_cast<uint32_t>(ch));
        pathKey.push_back(',');
    }
    return pathKey + "|" + entry + "|" + target;
}

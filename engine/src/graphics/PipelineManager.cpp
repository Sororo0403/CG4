#include "graphics/PipelineManager.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxUtils.h"
#include "graphics/ShaderCompiler.h"
#include <stdexcept>

using namespace DxUtils;

void PipelineManager::Initialize(DirectXCommon *dxCommon) {
    if (!dxCommon) {
        throw std::runtime_error("PipelineManager::Initialize null dxCommon");
    }
    dxCommon_ = dxCommon;
}

ID3DBlob *PipelineManager::CompileShader(const std::wstring &path,
                                         const std::string &entry,
                                         const std::string &target) {
    const std::string key = MakeShaderKey(path, entry, target);
    auto it = shaderCache_.find(key);
    if (it != shaderCache_.end()) {
        return it->second.Get();
    }

    auto shader = ShaderCompiler::Compile(path, entry, target);
    ID3DBlob *result = shader.Get();
    shaderCache_.emplace(key, std::move(shader));
    return result;
}

ID3D12PipelineState *PipelineManager::CreateGraphicsPipeline(
    const std::string &name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc) {
    if (!dxCommon_) {
        throw std::runtime_error("PipelineManager is not initialized");
    }

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    ThrowIfFailed(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
                      &desc, IID_PPV_ARGS(&pipeline)),
                  "Create managed graphics pipeline failed");

    ID3D12PipelineState *result = pipeline.Get();
    graphicsPipelines_[name] = std::move(pipeline);
    return result;
}

ID3D12PipelineState *
PipelineManager::GetGraphicsPipeline(const std::string &name) const {
    auto it = graphicsPipelines_.find(name);
    return it == graphicsPipelines_.end() ? nullptr : it->second.Get();
}

void PipelineManager::Clear() {
    graphicsPipelines_.clear();
    shaderCache_.clear();
}

std::string PipelineManager::MakeShaderKey(const std::wstring &path,
                                           const std::string &entry,
                                           const std::string &target) {
    std::string narrowPath;
    narrowPath.reserve(path.size());
    for (wchar_t ch : path) {
        narrowPath.push_back(static_cast<char>(ch & 0xff));
    }
    return narrowPath + "|" + entry + "|" + target;
}

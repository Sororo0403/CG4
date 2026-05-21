#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include <string>
#include <unordered_map>
#include <wrl.h>

class DirectXCommon;

class PipelineManager {
  public:
    void Initialize(DirectXCommon *dxCommon);

    ID3DBlob *CompileShader(const std::wstring &path, const std::string &entry,
                            const std::string &target);

    ID3D12PipelineState *
    CreateGraphicsPipeline(const std::string &name,
                           const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc);

    ID3D12PipelineState *GetGraphicsPipeline(const std::string &name) const;

    void Clear();

  private:
    static std::string MakeShaderKey(const std::wstring &path,
                                     const std::string &entry,
                                     const std::string &target);

    DirectXCommon *dxCommon_ = nullptr;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>>
        shaderCache_;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>>
        graphicsPipelines_;
};

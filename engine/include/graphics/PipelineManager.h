#pragma once
#include <d3d12.h>
#include <dxcapi.h>
#include <string>
#include <unordered_map>
#include <wrl.h>

class DirectXCommon;

class PipelineManager {
  public:
    /// <summary>
    /// 必要なリソースを初期化する
    /// </summary>
    void Initialize(DirectXCommon *dxCommon);

    IDxcBlob *CompileShader(const std::wstring &path, const std::string &entry,
                            const std::string &target);

    ID3D12PipelineState *
    CreateGraphicsPipeline(const std::string &name,
                           const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc);

    ID3D12PipelineState *GetGraphicsPipeline(const std::string &name) const;

    /// <summary>
    /// Clearを実行する
    /// </summary>
    void Clear();

  private:
    static std::string MakeShaderKey(const std::wstring &path,
                                     const std::string &entry,
                                     const std::string &target);

    DirectXCommon *dxCommon_ = nullptr;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<IDxcBlob>>
        shaderCache_;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>>
        graphicsPipelines_;
};

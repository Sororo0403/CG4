#pragma once
#include <d3d12.h>
#include <dxcapi.h>
#include <memory>
#include <string>

class DirectXCommon;

class PipelineManager {
public:
    PipelineManager();
    ~PipelineManager();

    /// <summary>
    /// 必要なリソースを初期化する
    /// </summary>
    void Initialize(DirectXCommon* dxCommon);

    IDxcBlob* CompileShader(const std::wstring& path, const std::string& entry,
                            const std::string& target);

    ID3D12PipelineState* CreateGraphicsPipeline(const std::string& name,
                                                const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc);
    ID3D12PipelineState* CreatePipelineStateStream(const std::string& name,
                                                   const D3D12_PIPELINE_STATE_STREAM_DESC& desc);

    ID3D12PipelineState* GetGraphicsPipeline(const std::string& name) const;

    /// <summary>
    /// Clearを実行する
    /// </summary>
    bool Clear();
    bool Clear(bool allowFrameAbort);

private:
    struct State;

    static std::string MakeShaderKey(const std::wstring& path, const std::string& entry,
                                     const std::string& target);

    DirectXCommon* dxCommon_ = nullptr;
    std::unique_ptr<State> state_;
};

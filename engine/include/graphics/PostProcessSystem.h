#pragma once
#include "graphics/PostProcessSettings.h"

#include <d3d12.h>
#include <memory>

class DirectXCommon;
class SrvManager;

class PostProcessSystem {
public:
    PostProcessSystem();
    ~PostProcessSystem();

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, int width, int height);
    /// <summary>
    /// Finalizeを実行する
    /// </summary>
    bool Finalize();
    bool Finalize(bool allowFrameAbort);

    bool Resize(int width, int height);

    void Draw(D3D12_GPU_DESCRIPTOR_HANDLE textureHandle, D3D12_GPU_DESCRIPTOR_HANDLE depthHandle);

    /// <summary>
    /// Profileを設定する
    /// </summary>
    void SetProfile(const PostProcessProfile& profile);

    const PostProcessProfile& GetProfile() const;
    /// <summary>
    /// RequiresPostProcessを実行する
    /// </summary>
    bool RequiresPostProcess() const;
    bool IsReady() const;

private:
    struct ConstantFrame;
    struct BloomResourceBuild;
    struct BloomDrawContext;
    struct DrawContext;
    struct State;

    /// <summary>
    /// RootSignatureを生成する
    /// </summary>
    void CreateRootSignature();

    void CreatePipelineState();
    void CreateBloomRootSignature();
    void CreateBloomPipelineState();

    /// <summary>
    /// ConstantBufferを生成する
    /// </summary>
    void CreateConstantBuffer();

    void UpdateConstantBuffer();
    ConstantFrame* GetCurrentConstantFrame();
    const ConstantFrame* GetCurrentConstantFrame() const;
    bool HasConstantBuffers() const;
    bool CreateBloomResources();
    bool BeginBloomResourceBuild(BloomResourceBuild& build);
    bool CreateBloomRtvHeap(BloomResourceBuild& build) const;
    bool CreateBloomLevelResources(BloomResourceBuild& build) const;
    bool CommitBloomResources(BloomResourceBuild& build);
    void RollbackBloomSrvRange(const BloomResourceBuild& build);
    bool ReleaseBloomResources(bool allowFrameAbort = false);
    void FreeBloomDescriptors();
    bool HasBloomResources() const;
    bool BuildBloom(D3D12_GPU_DESCRIPTOR_HANDLE sourceHandle,
                    const PostProcessConstants& constants);
    bool TryCreateBloomDrawContext(D3D12_GPU_DESCRIPTOR_HANDLE sourceHandle,
                                   const PostProcessConstants& constants,
                                   BloomDrawContext& context);
    bool DrawBloomLevel(BloomDrawContext& context, uint32_t targetLevel,
                        D3D12_GPU_DESCRIPTOR_HANDLE inputHandle, uint32_t sourceWidth,
                        uint32_t sourceHeight, ID3D12PipelineState* pipeline);
    bool RunBloomExtract(BloomDrawContext& context);
    bool RunBloomDownsampleChain(BloomDrawContext& context);
    bool RunBloomUpsampleChain(BloomDrawContext& context);
    bool HasDrawPipelineState() const;
    static bool IsValidDrawConstantFrame(const ConstantFrame* frame);
    bool ResolveDrawResources(DrawContext& context);
    PostProcessConstants PrepareDrawConstants(D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
                                              DrawContext& context);
    bool TryCreateDrawContext(D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
                              D3D12_GPU_DESCRIPTOR_HANDLE depthHandle, DrawContext& context);
    void BindDrawContext(const DrawContext& context, D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
                         D3D12_GPU_DESCRIPTOR_HANDLE depthHandle);
    static void DrawFullscreenTriangle(ID3D12GraphicsCommandList* commandList);
    bool TransitionBloomLevel(uint32_t level, D3D12_RESOURCE_STATES state);

    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    std::unique_ptr<State> state_;
};

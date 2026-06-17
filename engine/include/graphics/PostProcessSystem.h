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

    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager, int width,
                    int height);
    /// <summary>
    /// Finalizeを実行する
    /// </summary>
    bool Finalize();
    bool Finalize(bool allowFrameAbort);

    void Resize(int width, int height);

    void Draw(D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
              D3D12_GPU_DESCRIPTOR_HANDLE depthHandle);

    /// <summary>
    /// Profileを設定する
    /// </summary>
    void SetProfile(const PostProcessProfile &profile);

    const PostProcessProfile &GetProfile() const;
    /// <summary>
    /// RequiresPostProcessを実行する
    /// </summary>
    bool RequiresPostProcess() const;
    bool IsReady() const;

  private:
    struct ConstantFrame;
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
    ConstantFrame *GetCurrentConstantFrame();
    const ConstantFrame *GetCurrentConstantFrame() const;
    bool HasConstantBuffers() const;
    bool CreateBloomResources();
    bool ReleaseBloomResources(bool allowFrameAbort = false);
    void FreeBloomDescriptors();
    bool HasBloomResources() const;
    bool BuildBloom(D3D12_GPU_DESCRIPTOR_HANDLE sourceHandle,
                    const PostProcessConstants &constants);
    bool TransitionBloomLevel(uint32_t level, D3D12_RESOURCE_STATES state);

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    std::unique_ptr<State> state_;
};

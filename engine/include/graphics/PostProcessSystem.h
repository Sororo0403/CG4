#pragma once
#include "graphics/PostProcessSettings.h"
#include <d3d12.h>
#include <vector>
#include <wrl.h>

class DirectXCommon;
class SrvManager;

class PostProcessSystem {
  public:
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

    const PostProcessProfile &GetProfile() const { return profile_; }
    /// <summary>
    /// RequiresPostProcessを実行する
    /// </summary>
    bool RequiresPostProcess() const;
    bool IsReady() const {
        return dxCommon_ != nullptr && srvManager_ != nullptr &&
               rootSignature_ && pipelineState_ && copyPipelineState_ &&
               HasConstantBuffers();
    }

  private:
    struct ConstantFrame {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        PostProcessConstants *mapped = nullptr;

        void Reset() {
            if (resource && mapped != nullptr) {
                resource->Unmap(0, nullptr);
                mapped = nullptr;
            }
            resource.Reset();
        }
    };

    /// <summary>
    /// RootSignatureを生成する
    /// </summary>
    void CreateRootSignature();

    void CreatePipelineState();

    /// <summary>
    /// ConstantBufferを生成する
    /// </summary>
    void CreateConstantBuffer();

    void UpdateConstantBuffer();
    ConstantFrame *GetCurrentConstantFrame();
    const ConstantFrame *GetCurrentConstantFrame() const;
    bool HasConstantBuffers() const;

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> copyPipelineState_;
    std::vector<ConstantFrame> constantFrames_;
    PostProcessConstants constants_{};
    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissorRect_{};
    PostProcessProfile profile_{};
    int width_ = 1;
    int height_ = 1;
};

#pragma once
#include "Camera.h"
#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;
class TextureManager;
class SrvManager;

class SkyboxRenderer {
  public:
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    TextureManager *textureManager);

    void Draw(uint32_t textureId, const Camera &camera);

  private:
    void CreateRootSignature();
    void CreatePipelineState();
    void CreateMesh();
    void CreateConstantBuffer();

  private:
    struct ConstBufferData {
        DirectX::XMFLOAT4X4 matWVP{};
    };

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    TextureManager *textureManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;

    D3D12_VERTEX_BUFFER_VIEW vbView_{};
    D3D12_INDEX_BUFFER_VIEW ibView_{};

    ConstBufferData *mappedCB_ = nullptr;
    uint32_t indexCount_ = 0;
};

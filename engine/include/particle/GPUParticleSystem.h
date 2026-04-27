#pragma once
#include "Camera.h"
#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl.h>

class DirectXCommon;
class SrvManager;
class TextureManager;

/// <summary>
/// ComputeShaderで更新し、StructuredBufferを使ってインスタンス描画するGPU Particle
/// </summary>
class GPUParticleSystem {
  public:
    /// <summary>
    /// 初期化する
    /// </summary>
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    TextureManager *textureManager, uint32_t textureId,
                    uint32_t maxParticles = 1024);

    /// <summary>
    /// 更新する
    /// </summary>
    void Update(float deltaTime);

    /// <summary>
    /// 描画する
    /// </summary>
    void Draw(const Camera &camera);

    /// <summary>
    /// 発生位置を設定する
    /// </summary>
    void SetEmitterPosition(const DirectX::XMFLOAT3 &position) {
        emitterPosition_ = position;
    }

    /// <summary>
    /// 発生頻度と一度に出す数を設定する
    /// </summary>
    void SetEmission(uint32_t count, float frequency);

    /// <summary>
    /// 発生範囲の半径を設定する
    /// </summary>
    void SetEmitterRadius(float radius);

  private:
    struct ParticleForGPU {
        DirectX::XMFLOAT3 translate{};
        float currentTime = 0.0f;
        DirectX::XMFLOAT3 velocity{};
        float lifeTime = 1.0f;
        DirectX::XMFLOAT4 color{1.0f, 1.0f, 1.0f, 1.0f};
        DirectX::XMFLOAT2 scale{0.1f, 0.1f};
        float seed = 0.0f;
        uint32_t isActive = 0;
        DirectX::XMFLOAT3 padding{};
    };

    struct UpdateConstantBufferData {
        DirectX::XMFLOAT4 time{};
    };

    struct EmitterForGPU {
        DirectX::XMFLOAT3 translate{};
        float radius = 0.35f;
        uint32_t count = 10;
        float frequency = 0.5f;
        float frequencyTime = 0.0f;
        uint32_t emit = 0;
    };

    struct DrawConstantBufferData {
        DirectX::XMFLOAT4X4 viewProjection{};
        DirectX::XMFLOAT4 cameraRight{};
        DirectX::XMFLOAT4 cameraUp{};
        DirectX::XMFLOAT4 tintColor{};
    };

    void CreateRootSignatures();
    void CreatePipelineStates();
    void CreateParticleBuffer(const std::vector<ParticleForGPU> &particles);
    void CreateFreeListBuffers();
    void CreateConstantBuffers();

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    TextureManager *textureManager_ = nullptr;
    uint32_t textureId_ = 0;
    uint32_t maxParticles_ = 0;
    float totalTime_ = 0.0f;
    EmitterForGPU emitter_{};
    DirectX::XMFLOAT3 emitterPosition_{0.0f, 1.2f, 0.0f};

    Microsoft::WRL::ComPtr<ID3D12RootSignature> updateRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> drawRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> updatePSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> drawPSO_;

    Microsoft::WRL::ComPtr<ID3D12Resource> particleResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> particleUploadResource_;
    D3D12_GPU_DESCRIPTOR_HANDLE particleSrvGpuHandle_{};
    D3D12_CPU_DESCRIPTOR_HANDLE particleSrvCpuHandle_{};
    D3D12_GPU_DESCRIPTOR_HANDLE particleUavGpuHandle_{};
    D3D12_CPU_DESCRIPTOR_HANDLE particleUavCpuHandle_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> freeListResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListUploadResource_;
    D3D12_GPU_DESCRIPTOR_HANDLE freeListUavGpuHandle_{};
    D3D12_CPU_DESCRIPTOR_HANDLE freeListUavCpuHandle_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexUploadResource_;
    D3D12_GPU_DESCRIPTOR_HANDLE freeListIndexUavGpuHandle_{};
    D3D12_CPU_DESCRIPTOR_HANDLE freeListIndexUavCpuHandle_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> updateConstantBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> emitterConstantBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> drawConstantBuffer_;
    UpdateConstantBufferData *mappedUpdateCB_ = nullptr;
    EmitterForGPU *mappedEmitterCB_ = nullptr;
    DrawConstantBufferData *mappedDrawCB_ = nullptr;
};

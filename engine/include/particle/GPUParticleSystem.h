#pragma once
#include "camera/Camera.h"
#include "particle/ParticleEmitterSettings.h"
#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl.h>

class DirectXCommon;
class SrvManager;
class TextureManager;

/// <summary>
/// 計算シェーダーで更新し、構造化バッファを使ってインスタンス描画するGPUパーティクル。
/// </summary>
class GPUParticleSystem {
  public:
    /// <summary>
    /// GPUパーティクル用リソースを解放する
    /// </summary>
    ~GPUParticleSystem();

    /// <summary>
    /// GPUパーティクルの各種バッファ、SRV/UAV、描画設定を作成する
    /// </summary>
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    TextureManager *textureManager, uint32_t textureId,
                    uint32_t maxParticles = 1024);

    /// <summary>
    /// 経過時間とEmitter設定をGPUへ渡し、パーティクルシミュレーションを進める
    /// </summary>
    void Update(float deltaTime);

    /// <summary>
    /// カメラに向いたビルボードとして生存中のパーティクルを描画する
    /// </summary>
    void Draw(const Camera &camera);

    /// <summary>
    /// パーティクル発生設定を差し替える
    /// </summary>
    void SetEmitterSettings(const ParticleEmitterSettings &settings);

    /// <summary>
    /// 現在のパーティクル発生設定を取得する
    /// </summary>
    const ParticleEmitterSettings &GetEmitterSettings() const {
        return emitterSettings_;
    }

    /// <summary>
    /// 描画に使うテクスチャIDを切り替える
    /// </summary>
    void SetTexture(uint32_t textureId) { textureId_ = textureId; }

    /// <summary>
    /// TextureManager経由でテクスチャを読み込み、描画テクスチャを切り替える
    /// </summary>
    void SetTextureFromFile(const std::wstring &filePath);

    /// <summary>
    /// 指定した設定で一度だけ粒子を発生させる
    /// </summary>
    void EmitOnce(const ParticleEmitterSettings &settings);

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
        DirectX::XMFLOAT3 params{};
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
        DirectX::XMFLOAT4 tintColor{1.0f, 1.0f, 1.0f, 1.0f};
        DirectX::XMFLOAT4 directionSpeed{0.0f, 1.0f, 0.0f, 1.0f};
        uint32_t emissionMode = 0;
        float baseLifeTime = 0.34f;
        float lifeTimeRandom = 0.36f;
        float baseScale = 0.022f;
        float scaleRandom = 0.036f;
        float gravity = -1.70f;
        float turbulence = 0.25f;
        float alpha = 1.0f;
        float stretch = 4.8f;
        DirectX::XMFLOAT3 reserved{};
    };

    struct DrawConstantBufferData {
        DirectX::XMFLOAT4X4 viewProjection{};
        DirectX::XMFLOAT4 cameraRight{};
        DirectX::XMFLOAT4 cameraUp{};
        DirectX::XMFLOAT4 tintColor{};
    };

    /// <summary>
    /// 更新用と描画用のルートシグネチャを生成する
    /// </summary>
    void CreateRootSignatures();

    /// <summary>
    /// 更新用と描画用のパイプラインステートを生成する
    /// </summary>
    void CreatePipelineStates();

    /// <summary>
    /// パーティクルバッファを生成して初期データを書き込む
    /// </summary>
    void CreateParticleBuffer(const std::vector<ParticleForGPU> &particles);

    /// <summary>
    /// 空きリスト用バッファを生成する
    /// </summary>
    void CreateFreeListBuffers();

    /// <summary>
    /// 更新・Emitter・描画用の定数バッファを生成する
    /// </summary>
    void CreateConstantBuffers();

    /// <summary>
    /// パーティクル更新用ComputeShaderを実行する
    /// </summary>
    void DispatchUpdate();

    EmitterForGPU BuildEmitterForGPU(uint32_t emit) const;

    /// <summary>
    /// 保持しているGPUリソースを解放する
    /// </summary>
    void ReleaseResources();

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    TextureManager *textureManager_ = nullptr;
    uint32_t textureId_ = 0;
    uint32_t maxParticles_ = 0;
    float totalTime_ = 0.0f;
    float emitterFrequencyTime_ = 0.0f;
    bool updatePending_ = false;
    bool emitOncePending_ = false;
    ParticleEmitterSettings emitterSettings_{};

    Microsoft::WRL::ComPtr<ID3D12RootSignature> updateRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> drawRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> updatePSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> drawPSO_;

    Microsoft::WRL::ComPtr<ID3D12Resource> particleResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> particleUploadResource_;
    D3D12_GPU_DESCRIPTOR_HANDLE particleSrvGpuHandle_{};
    D3D12_CPU_DESCRIPTOR_HANDLE particleSrvCpuHandle_{};
    uint32_t particleSrvIndex_ = UINT32_MAX;
    D3D12_GPU_DESCRIPTOR_HANDLE particleUavGpuHandle_{};
    D3D12_CPU_DESCRIPTOR_HANDLE particleUavCpuHandle_{};
    uint32_t particleUavIndex_ = UINT32_MAX;

    Microsoft::WRL::ComPtr<ID3D12Resource> freeListResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListUploadResource_;
    D3D12_GPU_DESCRIPTOR_HANDLE freeListUavGpuHandle_{};
    D3D12_CPU_DESCRIPTOR_HANDLE freeListUavCpuHandle_{};
    uint32_t freeListUavIndex_ = UINT32_MAX;

    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexUploadResource_;
    D3D12_GPU_DESCRIPTOR_HANDLE freeListIndexUavGpuHandle_{};
    D3D12_CPU_DESCRIPTOR_HANDLE freeListIndexUavCpuHandle_{};
    uint32_t freeListIndexUavIndex_ = UINT32_MAX;

    Microsoft::WRL::ComPtr<ID3D12Resource> updateConstantBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> emitterConstantBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> drawConstantBuffer_;
    UpdateConstantBufferData *mappedUpdateCB_ = nullptr;
    EmitterForGPU *mappedEmitterCB_ = nullptr;
    DrawConstantBufferData *mappedDrawCB_ = nullptr;
};

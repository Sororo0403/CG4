#pragma once
#include "camera/Camera.h"
#include "core/ResourceHandle.h"
#include "particle/ParticleEmitterSettings.h"
#include "particle/ParticleFieldSettings.h"

#include <DirectXMath.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <deque>
#include <memory>
#include <string>
#include <vector>

inline constexpr uint32_t kInvalidParticleEmitterId = UINT32_MAX;

class DirectXCommon;
class SrvManager;
class TextureManager;

struct GPUParticleMaterialSettings {
    enum class BlendMode : uint32_t {
        Alpha = 0,
        Additive = 1,
    };

    std::wstring pixelShaderPath;
    DirectX::XMFLOAT4 params0{0.0f, 0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 params1{0.0f, 0.0f, 0.0f, 0.0f};
    uint32_t noiseTextureId = kInvalidResourceId;
    BlendMode blendMode = BlendMode::Alpha;
};

struct GPUParticleLightingSettings {
    struct PointLight {
        DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
        float range = 1.0f;
        DirectX::XMFLOAT4 color{1.0f, 1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
    };

    DirectX::XMFLOAT3 direction{-0.35f, -0.95f, 0.25f};
    float intensity = 1.0f;
    DirectX::XMFLOAT4 color{1.0f, 0.96f, 0.9f, 1.0f};
    DirectX::XMFLOAT4 ambient{0.2f, 0.22f, 0.26f, 1.0f};
    std::array<PointLight, 4> pointLights{};
    uint32_t pointLightCount = 0;
};

struct GPUParticleExplicitSpawn {
    DirectX::XMFLOAT4 positionLife{0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4 velocityStartScale{0.0f, 0.0f, 0.0f, 0.1f};
    DirectX::XMFLOAT4 color{1.0f, 1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT4 scaleFade{0.0f, 0.0f, 0.2f, 1.0f};
    DirectX::XMFLOAT4 motion{0.0f, 0.0f, 0.98f, 0.0f};
    DirectX::XMFLOAT4 accelerationAtlas{0.0f, 0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 drawAxis{0.0f, 0.0f, 0.0f, 0.0f};
    DirectX::XMUINT4 atlas{1u, 1u, 1u, 0u};
};

/// <summary>
/// 計算シェーダーで更新し、構造化バッファを使ってインスタンス描画するGPUパーティクル。
/// </summary>
class GPUParticleSystem {
public:
    /// <summary>
    /// 共有しているGPUパーティクル描画キャッシュを解放する
    /// </summary>
    static void ReleaseSharedResources();

    /// <summary>
    /// GPUパーティクル用リソースを解放する
    /// </summary>
    GPUParticleSystem();
    ~GPUParticleSystem();

    /// <summary>
    /// GPUパーティクルの各種バッファ、SRV/UAV、描画設定を作成する
    /// </summary>
    bool Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, TextureManager* textureManager,
                    uint32_t textureId, uint32_t maxParticles = 1024);

    /// <summary>
    /// GPUパーティクル用リソースを解放する
    /// </summary>
    bool Release();

    /// <summary>
    /// 経過時間とEmitter設定をGPUへ渡し、パーティクルシミュレーションを進める
    /// </summary>
    void Update(float deltaTime);

    /// <summary>
    /// 現在残っている粒子と保留中の発生要求を消去する
    /// </summary>
    void Clear();

    /// <summary>
    /// カメラに向いたビルボードとして生存中のパーティクルを描画する
    /// </summary>
    void Draw(const Camera& camera);

    /// <summary>
    /// 保留中のGPU更新を描画とは別に実行する
    /// </summary>
    void DispatchPendingUpdate();

    /// <summary>
    /// パーティクル発生設定を差し替える
    /// </summary>
    void SetEmitterSettings(const ParticleEmitterSettings& settings);

    /// <summary>
    /// 現在のパーティクル発生設定を取得する
    /// </summary>
    const ParticleEmitterSettings& GetEmitterSettings() const {
        return emitterSettings_;
    }

    /// <summary>
    /// 描画に使うテクスチャIDを切り替える
    /// </summary>
    void SetTexture(uint32_t textureId) {
        textureId_ = textureId;
    }

    /// <summary>
    /// 描画用PixelShaderとマテリアル定数を設定する
    /// </summary>
    void SetMaterialSettings(const GPUParticleMaterialSettings& settings);
    void SetLightingSettings(const GPUParticleLightingSettings& settings);

    /// <summary>
    /// TextureManager経由でテクスチャを読み込み、描画テクスチャを切り替える
    /// </summary>
    void SetTextureFromFile(const std::wstring& filePath);

    /// <summary>
    /// 指定した設定で一度だけ粒子を発生させる
    /// </summary>
    void EmitOnce(const ParticleEmitterSettings& settings);

    /// <summary>
    /// 位置・速度・色を明示した粒子群を一度だけ発生させる
    /// </summary>
    size_t EmitParticles(const std::vector<GPUParticleExplicitSpawn>& particles);

    uint32_t AddEmitter(const ParticleEmitterSettings& settings, bool enabled = true);
    bool UpdateEmitter(uint32_t emitterId, const ParticleEmitterSettings& settings);
    bool RemoveEmitter(uint32_t emitterId);
    bool SetEmitterEnabled(uint32_t emitterId, bool enabled);
    bool GetEmitterSettings(uint32_t emitterId, ParticleEmitterSettings& settings) const;
    size_t GetEmitterCount() const {
        return managedEmitters_.size();
    }

    void SetFields(const std::vector<ParticleFieldSettings>& fields);
    const std::vector<ParticleFieldSettings>& GetFields() const {
        return fields_;
    }

private:
    class InitializationGuard;
    struct ConstantFrame;
    struct ExplicitSpawnFrame;
    struct ResourceState;
    struct ManagedEmitter {
        uint32_t id = kInvalidParticleEmitterId;
        ParticleEmitterSettings settings{};
        float frequencyTime = 0.0f;
        bool enabled = true;
    };

    struct ParticleForGPU {
        DirectX::XMFLOAT3 translate{};
        float currentTime = 0.0f;
        DirectX::XMFLOAT3 velocity{};
        float lifeTime = 1.0f;
        DirectX::XMFLOAT4 color{1.0f, 1.0f, 1.0f, 1.0f};
        DirectX::XMFLOAT2 scale{0.1f, 0.1f};
        float seed = 0.0f;
        uint32_t isActive = 0;
        DirectX::XMFLOAT4 params0{};
        DirectX::XMFLOAT4 params1{};
        DirectX::XMFLOAT4 params2{};
        DirectX::XMFLOAT4 params3{};
        DirectX::XMFLOAT4 params4{};
    };

    struct UpdateConstantBufferData {
        DirectX::XMFLOAT4 time{};
        struct FieldForGPU {
            DirectX::XMFLOAT4 positionRadius{};
            DirectX::XMFLOAT4 directionStrength{};
            DirectX::XMFLOAT4 params{};
        };
        std::array<FieldForGPU, 8> fields{};
        DirectX::XMUINT4 fieldConfig{};
    };

    struct EmitterForGPU {
        DirectX::XMFLOAT4 position{};
        DirectX::XMFLOAT4 spawnOffsetScale{0.1f, 0.1f, 0.1f, 0.0f};
        DirectX::XMFLOAT4 basisRight{1.0f, 0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT4 basisUp{0.0f, 1.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT4 basisForward{0.0f, 0.0f, 1.0f, 0.0f};
        DirectX::XMFLOAT4 directionAndDirectionalVelocity{0.0f, 1.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT4 velocityBiasAndRadialVelocity{0.0f, 0.0f, 0.0f, 1.0f};
        DirectX::XMFLOAT4 lifeAndFade{0.5f, 0.2f, 0.0f, 0.2f};
        DirectX::XMFLOAT4 scale{0.2f, 0.0f, 0.1f, 0.0f};
        DirectX::XMFLOAT4 accelerationAndTurbulence{};
        DirectX::XMFLOAT4 motion{1.0f, 1.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT4 atlasAndRotation{0.0f, 1.0f, 0.7f, 0.0f};
        DirectX::XMFLOAT4 tintColor{1.0f, 1.0f, 1.0f, 1.0f};
        DirectX::XMUINT3 config{};
    };

    struct DrawConstantBufferData {
        DirectX::XMFLOAT4X4 viewProjection{};
        DirectX::XMFLOAT4 cameraRight{};
        DirectX::XMFLOAT4 cameraUp{};
        DirectX::XMFLOAT4 tintColor{};
        DirectX::XMFLOAT4 atlasInfo{1.0f, 1.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT4 materialParams0{};
        DirectX::XMFLOAT4 materialParams1{};
        DirectX::XMFLOAT4 lightDirectionIntensity{};
        DirectX::XMFLOAT4 lightColor{};
        DirectX::XMFLOAT4 ambientColor{};
        std::array<DirectX::XMFLOAT4, 4> pointLightPositionRange{};
        std::array<DirectX::XMFLOAT4, 4> pointLightColorIntensity{};
        DirectX::XMUINT4 lightConfig{};
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
    void CreateParticleBuffer(const std::vector<ParticleForGPU>& particles);

    /// <summary>
    /// 空きリスト用バッファを生成する
    /// </summary>
    void CreateFreeListBuffers();
    bool CreateFreeListResource(ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
                                UINT bufferSize);
    bool CreateFreeListIndexResource(ID3D12Device* device, ID3D12GraphicsCommandList* commandList);
    void CreateActiveDrawBuffers();
    bool CreateActiveIndexBuffer(ID3D12Device* device, UINT bufferSize);
    bool CreateActiveCountBuffer(ID3D12Device* device);
    bool CreateDrawArgsBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* commandList);

    /// <summary>
    /// 更新・描画用の定数バッファを生成する
    /// </summary>
    void CreateConstantBuffers();

    /// <summary>
    /// パーティクル更新用ComputeShaderを実行する
    /// </summary>
    void DispatchUpdate();
    bool HasUpdateDispatchResources() const;
    bool BindDescriptorHeap(ID3D12GraphicsCommandList*& commandList) const;
    bool RegisterUpdateDispatchRollback(
        D3D12_RESOURCE_STATES previousActiveIndexState, D3D12_RESOURCE_STATES previousDrawArgsState,
        bool previousUpdatePending, bool previousClearPending,
        const std::deque<ParticleEmitterSettings>& previousPendingEmitSettings,
        const std::vector<GPUParticleExplicitSpawn>& previousPendingExplicitParticles);
    void TransitionUpdateResourcesToUav(ID3D12GraphicsCommandList* commandList);
    void ClearUpdateCounters(ID3D12GraphicsCommandList* commandList);
    void RecordUpdateUavBarrier(ID3D12GraphicsCommandList* commandList);
    void RecordQueuedEmitterDispatches(ID3D12GraphicsCommandList* commandList,
                                       const std::deque<ParticleEmitterSettings>& emitSettings);
    bool RecordExplicitParticleDispatches(ID3D12GraphicsCommandList* commandList,
                                          std::vector<GPUParticleExplicitSpawn> explicitParticles);
    void TransitionUpdateResourcesForDraw(ID3D12GraphicsCommandList* commandList);
    void RecordUpdateDispatch(const EmitterForGPU& emitter, uint32_t dispatchParticleCount = 0u);
    void RecordExplicitSpawnDispatch(uint32_t spawnCount);
    bool EnsureExplicitSpawnCapacity(uint32_t capacity);
    bool EnsureExplicitSpawnFrameCapacity(ExplicitSpawnFrame& frame, uint32_t capacity);
    bool UploadExplicitParticles(const std::vector<GPUParticleExplicitSpawn>& particles,
                                 uint32_t& uploadedCount);
    void UpdateManagedEmitters(float deltaTime);
    void QueueContinuousEmitter(const ParticleEmitterSettings& settings, float deltaTime,
                                float& frequencyTime);
    void QueueMeshSurfaceEmission(const ParticleEmitterSettings& settings);
    void UpdateFieldConstants();

    EmitterForGPU BuildEmitterForGPU(const ParticleEmitterSettings& settings, uint32_t emit) const;
    ConstantFrame* GetCurrentConstantFrame();
    const ConstantFrame* GetCurrentConstantFrame() const;
    bool HasConstantBuffers() const;
    bool ConfigureInitialState(uint32_t textureId, uint32_t maxParticles,
                               std::deque<ParticleEmitterSettings> pendingEmitSettings);
    std::vector<ParticleForGPU> CreateInitialParticleData() const;
    bool CreateInitializationGpuResources(const std::vector<ParticleForGPU>& particles);
    bool HasRequiredGpuResources() const;
    void QueueInitialUpdateIfNeeded();
    bool HasDrawResources() const;
    bool ShouldSkipDraw() const;
    ConstantFrame* PrepareDrawFrame(ID3D12GraphicsCommandList*& commandList);
    void UpdateDrawConstants(const Camera& camera, ConstantFrame& constantFrame) const;
    bool ResolveDrawTextureHandles(D3D12_GPU_DESCRIPTOR_HANDLE& baseTextureHandle,
                                   D3D12_GPU_DESCRIPTOR_HANDLE& noiseTextureHandle) const;
    void RecordDrawCommands(ID3D12GraphicsCommandList* commandList, ConstantFrame& constantFrame,
                            D3D12_GPU_DESCRIPTOR_HANDLE baseTextureHandle,
                            D3D12_GPU_DESCRIPTOR_HANDLE noiseTextureHandle);

    /// <summary>
    /// 保持しているGPUリソースを解放する
    /// </summary>
    bool ReleaseResources();
    bool ReleaseResources(bool allowFrameAbort);

    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    TextureManager* textureManager_ = nullptr;
    uint32_t textureId_ = 0;
    uint32_t maxParticles_ = 0;
    float totalTime_ = 0.0f;
    float emitterFrequencyTime_ = 0.0f;
    float activeTimeRemaining_ = 0.0f;
    bool updatePending_ = false;
    bool clearPending_ = false;
    ParticleEmitterSettings emitterSettings_{};
    std::deque<ParticleEmitterSettings> pendingEmitSettings_;
    std::vector<GPUParticleExplicitSpawn> pendingExplicitParticles_;
    std::vector<ManagedEmitter> managedEmitters_;
    std::vector<ParticleFieldSettings> fields_;
    uint32_t nextEmitterId_ = 1u;
    GPUParticleMaterialSettings materialSettings_{};
    GPUParticleLightingSettings lightingSettings_{};
    uint32_t meshRandomState_ = 0xA341316Cu;

    std::unique_ptr<ResourceState> resources_;
};

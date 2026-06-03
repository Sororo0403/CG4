#include "particle/GPUParticleSystem.h"
#include "GPUParticleEmitterUtils.h"
#include "GPUParticleSystemShared.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <random>

using namespace DirectX;

namespace {

constexpr uint32_t kParticleThreadCount = 256u;
constexpr size_t kMaxQueuedParticleEmitsPerFrame = 128u;
constexpr uint32_t kMaxGpuParticles = 1'048'576u;
constexpr UINT kRequiredSrvDescriptors = 8u;

using GpuParticleEmitterUtils::EstimateParticleActiveDuration;
using GpuParticleEmitterUtils::IsContinuousEmitter;
using GpuParticleEmitterUtils::NormalizeParticleEmitterSettings;
using GpuParticleEmitterUtils::ResolveTextureId;
using GpuParticleEmitterUtils::SanitizeFinite;

UINT CheckedByteSize(size_t elementSize, size_t count, const char *message) {
    (void)message;
    if (count == 0 ||
        elementSize > (std::numeric_limits<size_t>::max)() / count) {
        return 0;
    }
    const size_t bytes = elementSize * count;
    if (bytes > (std::numeric_limits<UINT>::max)()) {
        return 0;
    }
    return static_cast<UINT>(bytes);
}

class ParticleUploadPassScope {
  public:
    ParticleUploadPassScope(DirectXCommon *dxCommon, bool active)
        : dxCommon_(dxCommon), active_(active) {}

    ~ParticleUploadPassScope() {
        if (active_ && dxCommon_ != nullptr) {
            dxCommon_->AbortFrame();
        }
    }

    ParticleUploadPassScope(const ParticleUploadPassScope &) = delete;
    ParticleUploadPassScope &
    operator=(const ParticleUploadPassScope &) = delete;

    bool Finish() {
        if (!active_) {
            return true;
        }
        if (dxCommon_->EndUploadPass() ==
            DirectXCommon::UploadPassResult::Failed) {
            return false;
        }
        active_ = false;
        return true;
    }

  private:
    DirectXCommon *dxCommon_ = nullptr;
    bool active_ = false;
};

}

GPUParticleSystem::~GPUParticleSystem() {
    ReleaseResources(true);
}

class GPUParticleSystem::InitializationGuard {
  public:
    explicit InitializationGuard(GPUParticleSystem &system) : system_(system) {}
    ~InitializationGuard() {
        if (active_) {
            system_.ReleaseResources(true);
        }
    }

    InitializationGuard(const InitializationGuard &) = delete;
    InitializationGuard &operator=(const InitializationGuard &) = delete;

    void Commit() { active_ = false; }

  private:
    GPUParticleSystem &system_;
    bool active_ = true;
};

void GPUParticleSystem::ReleaseSharedResources() {
    GpuParticleShared::ReleaseDrawResources();
}

void GPUParticleSystem::Initialize(DirectXCommon *dxCommon,
                                   SrvManager *srvManager,
                                   TextureManager *textureManager,
                                   uint32_t textureId, uint32_t maxParticles) {
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager || !textureManager) {
        ReleaseResources(true);
        return;
    }

    std::vector<ParticleEmitterSettings> pendingBeforeInitialize;
    pendingBeforeInitialize.swap(pendingEmitSettings_);
    if (!ReleaseResources(true)) {
        pendingEmitSettings_ = std::move(pendingBeforeInitialize);
        return;
    }
    if (dxCommon->IsCommandListRecording() && !dxCommon->IsUploadPassActive()) {
        pendingEmitSettings_ = std::move(pendingBeforeInitialize);
        return;
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    textureManager_ = textureManager;
    InitializationGuard initializeGuard(*this);
    textureId_ = textureId;
    maxParticles_ = std::clamp(maxParticles, 1u, kMaxGpuParticles);
    if (CheckedByteSize(sizeof(ParticleForGPU), maxParticles_,
                        "GPUParticleSystem particle buffer size overflow") ==
            0 ||
        CheckedByteSize(sizeof(uint32_t), maxParticles_,
                        "GPUParticleSystem index buffer size overflow") == 0) {
        return;
    }
    if (!srvManager_->CanAllocateDescriptors(kRequiredSrvDescriptors)) {
        return;
    }
    totalTime_ = 0.0f;
    emitterFrequencyTime_ = 0.0f;
    activeTimeRemaining_ = 0.0f;
    emitterSettings_ = NormalizeParticleEmitterSettings(ParticleEmitterSettings{});
    pendingEmitSettings_ = std::move(pendingBeforeInitialize);
    for (const ParticleEmitterSettings &settings : pendingEmitSettings_) {
        emitterSettings_ = settings;
        activeTimeRemaining_ =
            (std::max)(activeTimeRemaining_,
                       EstimateParticleActiveDuration(settings));
    }

    std::mt19937 randomEngine{std::random_device{}()};
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    std::vector<ParticleForGPU> particles(maxParticles_);
    for (ParticleForGPU &particle : particles) {
        particle.translate = emitterSettings_.position;
        particle.velocity = {};
        particle.lifeTime = 1.0f;
        particle.currentTime = particle.lifeTime;
        particle.color = {1.0f, 1.0f, 1.0f, 0.0f};
        particle.scale = {0.0f, 0.0f};
        particle.seed = dist01(randomEngine) * 10000.0f;
        particle.isActive = 0;
        particle.params0 = {};
        particle.params1 = {};
        particle.params2 = {};
        particle.params3 = {};
    }

    CreateRootSignatures();
    CreatePipelineStates();
    if (!updateRootSignature_ || !drawRootSignature_ || !updatePSO_ ||
        !drawPSO_ || !drawCommandSignature_) {
        return;
    }

    const bool ownsUploadPass = !dxCommon_->IsCommandListRecording();
    if (ownsUploadPass && !dxCommon_->BeginUpload()) {
        return;
    }
    ParticleUploadPassScope uploadPass(dxCommon_, ownsUploadPass);
    if (!dxCommon_->IsCommandListRecording()) {
        return;
    }
    CreateParticleBuffer(particles);
    CreateFreeListBuffers();
    CreateActiveDrawBuffers();
    if (!uploadPass.Finish()) {
        return;
    }

    CreateConstantBuffers();

    if (!particleResource_ || !freeListResource_ ||
        !freeListIndexResource_ || !activeIndexResource_ ||
        !activeCountResource_ || !drawArgsResource_ ||
        !HasConstantBuffers() ||
        particleSrvGpuHandle_.ptr == 0 || particleUavGpuHandle_.ptr == 0 ||
        freeListUavGpuHandle_.ptr == 0 ||
        freeListIndexUavGpuHandle_.ptr == 0 ||
        activeIndexSrvGpuHandle_.ptr == 0 ||
        activeIndexUavGpuHandle_.ptr == 0 ||
        activeCountUavGpuHandle_.ptr == 0 ||
        drawArgsUavGpuHandle_.ptr == 0) {
        return;
    }

    if (!pendingEmitSettings_.empty() && HasConstantBuffers()) {
        updateConstants_.time = {totalTime_, 0.0f,
                                 static_cast<float>(maxParticles_), 0.0f};
        updatePending_ = true;
    }
    initializeGuard.Commit();
}

void GPUParticleSystem::SetEmitterSettings(
    const ParticleEmitterSettings &settings) {
    ParticleEmitterSettings normalized = NormalizeParticleEmitterSettings(settings);
    const bool keepFrequencyTime =
        IsContinuousEmitter(emitterSettings_) && IsContinuousEmitter(normalized) &&
        std::abs(emitterSettings_.emitRate - normalized.emitRate) < 0.0001f;
    emitterSettings_ = normalized;
    if (!keepFrequencyTime) {
        emitterFrequencyTime_ = 0.0f;
    }
}

void GPUParticleSystem::SetTextureFromFile(const std::wstring &filePath) {
    if (!textureManager_) {
        textureId_ = UINT32_MAX;
        return;
    }

    textureId_ = textureManager_->Load(filePath);
}

void GPUParticleSystem::SetMaterialSettings(
    const GPUParticleMaterialSettings &settings) {
    materialSettings_ = settings;
    materialSettings_.params0 = SanitizeFinite(materialSettings_.params0, {});
    materialSettings_.params1 = SanitizeFinite(materialSettings_.params1, {});
    if (dxCommon_ && dxCommon_->GetDevice() && drawRootSignature_) {
        const std::wstring pixelShaderPath =
            materialSettings_.pixelShaderPath.empty()
                ? std::wstring(ShaderPaths::ParticlePS)
                : materialSettings_.pixelShaderPath;
        drawPSO_ = GpuParticleShared::GetOrCreateDrawPipeline(
            dxCommon_->GetDevice(), drawRootSignature_.Get(), pixelShaderPath);
    }
}
void GPUParticleSystem::EmitOnce(const ParticleEmitterSettings &settings) {
    ParticleEmitterSettings normalized = NormalizeParticleEmitterSettings(settings);
    emitterSettings_ = normalized;
    emitterFrequencyTime_ = 0.0f;
    activeTimeRemaining_ =
        (std::max)(activeTimeRemaining_,
                   EstimateParticleActiveDuration(normalized));
    if (pendingEmitSettings_.size() >= kMaxQueuedParticleEmitsPerFrame) {
        pendingEmitSettings_.erase(pendingEmitSettings_.begin());
    }
    try {
        pendingEmitSettings_.push_back(normalized);
    } catch (...) {
        return;
    }
    if (HasConstantBuffers() && !updatePending_) {
        updateConstants_.time = {totalTime_, 0.0f,
                                 static_cast<float>(maxParticles_), 0.0f};
        updatePending_ = true;
    }
}

GPUParticleSystem::ConstantFrame *GPUParticleSystem::GetCurrentConstantFrame() {
    if (constantFrames_.empty()) {
        return nullptr;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr
            ? dxCommon_->GetBackBufferIndex() % constantFrames_.size()
            : 0;
    return &constantFrames_[frameIndex];
}

const GPUParticleSystem::ConstantFrame *
GPUParticleSystem::GetCurrentConstantFrame() const {
    if (constantFrames_.empty()) {
        return nullptr;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr
            ? dxCommon_->GetBackBufferIndex() % constantFrames_.size()
            : 0;
    return &constantFrames_[frameIndex];
}

bool GPUParticleSystem::HasConstantBuffers() const {
    if (constantFrames_.empty()) {
        return false;
    }
    for (const ConstantFrame &frame : constantFrames_) {
        if (!frame.updateConstantBuffer || !frame.drawConstantBuffer ||
            frame.mappedUpdateCB == nullptr || frame.mappedDrawCB == nullptr) {
            return false;
        }
    }
    return true;
}

void GPUParticleSystem::Clear() {
    if (!dxCommon_ || !srvManager_ || !textureManager_ || maxParticles_ == 0) {
        pendingEmitSettings_.clear();
        activeTimeRemaining_ = 0.0f;
        updatePending_ = false;
        emitterFrequencyTime_ = 0.0f;
        return;
    }

    DirectXCommon *dxCommon = dxCommon_;
    SrvManager *srvManager = srvManager_;
    TextureManager *textureManager = textureManager_;
    const uint32_t textureId = textureId_;
    const uint32_t maxParticles = maxParticles_;
    const ParticleEmitterSettings emitterSettings = emitterSettings_;
    const GPUParticleMaterialSettings materialSettings = materialSettings_;

    Initialize(dxCommon, srvManager, textureManager, textureId, maxParticles);
    SetEmitterSettings(emitterSettings);
    SetMaterialSettings(materialSettings);
    pendingEmitSettings_.clear();
    activeTimeRemaining_ = 0.0f;
    updatePending_ = false;
    emitterFrequencyTime_ = 0.0f;
}

void GPUParticleSystem::Update(float deltaTime) {
    deltaTime = std::clamp(SanitizeFinite(deltaTime, 0.0f), 0.0f, 0.1f);
    totalTime_ += deltaTime;

    if (!HasConstantBuffers()) {
        return;
    }

    const bool continuousEmitter = IsContinuousEmitter(emitterSettings_);
    const bool wasActive = activeTimeRemaining_ > 0.0f;

    if (wasActive) {
        activeTimeRemaining_ =
            (std::max)(0.0f, activeTimeRemaining_ - deltaTime);
    }

    if (continuousEmitter) {
        emitterFrequencyTime_ += deltaTime;
        const float safeEmitRate =
            (std::max)(SanitizeFinite(emitterSettings_.emitRate, 0.0f),
                       0.0001f);
        const float interval = 1.0f / safeEmitRate;
        while (emitterFrequencyTime_ >= interval &&
               pendingEmitSettings_.size() < kMaxQueuedParticleEmitsPerFrame) {
            emitterFrequencyTime_ -= interval;
            try {
                pendingEmitSettings_.push_back(emitterSettings_);
            } catch (...) {
                break;
            }
            activeTimeRemaining_ =
                (std::max)(activeTimeRemaining_,
                           EstimateParticleActiveDuration(emitterSettings_));
        }
        if (emitterFrequencyTime_ >= interval) {
            emitterFrequencyTime_ = std::fmod(emitterFrequencyTime_, interval);
        }
    }

    if (pendingEmitSettings_.empty() && !continuousEmitter && !wasActive) {
        return;
    }

    updateConstants_.time = {totalTime_, deltaTime,
                             static_cast<float>(maxParticles_), 0.0f};

    updatePending_ = true;
    if (dxCommon_ && dxCommon_->IsCommandListRecording()) {
        DispatchUpdate();
    }
}
void GPUParticleSystem::Draw(const Camera &camera) {
    if (!dxCommon_ || !srvManager_ || !textureManager_ ||
        !dxCommon_->IsCommandListRecording() ||
        !particleResource_ || !activeIndexResource_ || !drawArgsResource_ ||
        !drawCommandSignature_ || !drawRootSignature_ || !drawPSO_ ||
        !HasConstantBuffers() ||
        particleSrvGpuHandle_.ptr == 0 || activeIndexSrvGpuHandle_.ptr == 0) {
        return;
    }
    if (!updatePending_ && pendingEmitSettings_.empty() &&
        activeTimeRemaining_ <= 0.0f &&
        !IsContinuousEmitter(emitterSettings_)) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    if (cmd == nullptr || heap == nullptr) {
        return;
    }
    ConstantFrame *constantFrame = GetCurrentConstantFrame();
    if (constantFrame == nullptr ||
        !constantFrame->drawConstantBuffer ||
        constantFrame->mappedDrawCB == nullptr) {
        return;
    }
    ID3D12DescriptorHeap *heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);

    if (updatePending_ && dxCommon_->IsCommandListRecording()) {
        DispatchUpdate();
    }

    XMMATRIX viewProjection = camera.GetView() * camera.GetProj();
    XMStoreFloat4x4(&constantFrame->mappedDrawCB->viewProjection,
                    XMMatrixTranspose(viewProjection));

    XMMATRIX billboard = camera.GetView();
    billboard.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    const XMVECTOR billboardDeterminant = XMMatrixDeterminant(billboard);
    const float billboardDeterminantValue =
        XMVectorGetX(billboardDeterminant);
    billboard = std::isfinite(billboardDeterminantValue) &&
                        std::abs(billboardDeterminantValue) > 0.000001f
                    ? XMMatrixInverse(nullptr, billboard)
                    : XMMatrixIdentity();

    XMFLOAT3 right{};
    XMFLOAT3 up{};
    XMStoreFloat3(&right, billboard.r[0]);
    XMStoreFloat3(&up, billboard.r[1]);
    constantFrame->mappedDrawCB->cameraRight = {right.x, right.y, right.z, 0.0f};
    constantFrame->mappedDrawCB->cameraUp = {up.x, up.y, up.z, 0.0f};
    constantFrame->mappedDrawCB->tintColor = {1.0f, 1.0f, 1.0f, 1.0f};
    constantFrame->mappedDrawCB->atlasInfo = {
        static_cast<float>((std::max)(1u, emitterSettings_.atlasColumns)),
        static_cast<float>((std::max)(1u, emitterSettings_.atlasRows)),
        0.0f,
        0.0f};
    constantFrame->mappedDrawCB->materialParams0 = materialSettings_.params0;
    constantFrame->mappedDrawCB->materialParams1 = materialSettings_.params1;

    const uint32_t whiteTextureId = textureManager_->GetWhiteTextureId();
    const uint32_t noiseTextureId = ResolveTextureId(
        textureManager_, materialSettings_.noiseTextureId, whiteTextureId);
    const uint32_t baseTextureId =
        ResolveTextureId(textureManager_, textureId_, whiteTextureId);
    const D3D12_GPU_DESCRIPTOR_HANDLE baseTextureHandle =
        textureManager_->GetGpuHandle(baseTextureId);
    const D3D12_GPU_DESCRIPTOR_HANDLE noiseTextureHandle =
        textureManager_->GetGpuHandle(noiseTextureId);
    if (baseTextureHandle.ptr == 0 || noiseTextureHandle.ptr == 0) {
        return;
    }

    cmd->SetGraphicsRootSignature(drawRootSignature_.Get());
    cmd->SetPipelineState(drawPSO_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootConstantBufferView(
        0, constantFrame->drawConstantBuffer->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, particleSrvGpuHandle_);
    cmd->SetGraphicsRootDescriptorTable(2, baseTextureHandle);
    cmd->SetGraphicsRootDescriptorTable(3, noiseTextureHandle);
    cmd->SetGraphicsRootDescriptorTable(4, activeIndexSrvGpuHandle_);
    cmd->ExecuteIndirect(drawCommandSignature_.Get(), 1, drawArgsResource_.Get(),
                         0, nullptr, 0);
}
void GPUParticleSystem::DispatchPendingUpdate() {
    if (updatePending_ && dxCommon_ && dxCommon_->IsCommandListRecording()) {
        DispatchUpdate();
    }
}

void GPUParticleSystem::DispatchUpdate() {
    if (!dxCommon_ || !srvManager_ || !particleResource_ ||
        !dxCommon_->IsCommandListRecording() ||
        !activeIndexResource_ || !activeCountResource_ || !drawArgsResource_ ||
        !freeListResource_ || !freeListIndexResource_ ||
        !updateRootSignature_ || !updatePSO_ || !HasConstantBuffers() ||
        particleUavGpuHandle_.ptr == 0 || freeListUavGpuHandle_.ptr == 0 ||
        freeListIndexUavGpuHandle_.ptr == 0 ||
        activeIndexUavGpuHandle_.ptr == 0 ||
        activeCountUavGpuHandle_.ptr == 0 ||
        drawArgsUavGpuHandle_.ptr == 0) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    if (cmd == nullptr || heap == nullptr) {
        return;
    }
    ID3D12DescriptorHeap *heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);

    const D3D12_RESOURCE_STATES previousActiveIndexState = activeIndexState_;
    const D3D12_RESOURCE_STATES previousDrawArgsState = drawArgsState_;
    const bool previousUpdatePending = updatePending_;
    std::function<void()> rollback;
    try {
        std::vector<ParticleEmitterSettings> previousPendingEmitSettings =
            pendingEmitSettings_;
        rollback =
            [this, previousActiveIndexState, previousDrawArgsState,
             previousUpdatePending,
             previousPendingEmitSettings =
                 std::move(previousPendingEmitSettings)]() mutable {
            activeIndexState_ = previousActiveIndexState;
            drawArgsState_ = previousDrawArgsState;
            updatePending_ = previousUpdatePending;
            pendingEmitSettings_.swap(previousPendingEmitSettings);
        };
    } catch (...) {
        return;
    }
    if (!dxCommon_->RegisterFrameRollback(this, std::move(rollback))) {
        return;
    }

    D3D12_RESOURCE_BARRIER barriers[3]{};
    UINT barrierCount = 0;
    barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(
        particleResource_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (activeIndexState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(
            activeIndexResource_.Get(), activeIndexState_,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        activeIndexState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (drawArgsState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(
            drawArgsResource_.Get(), drawArgsState_,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        drawArgsState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    cmd->ResourceBarrier(barrierCount, barriers);

    const UINT clearValues[4] = {};
    cmd->ClearUnorderedAccessViewUint(activeCountUavGpuHandle_,
                                      activeCountUavCpuHandle_,
                                      activeCountResource_.Get(), clearValues,
                                      0, nullptr);
    const UINT drawArgsClearValues[4] = {6u, 0u, 0u, 0u};
    cmd->ClearUnorderedAccessViewUint(drawArgsUavGpuHandle_,
                                      drawArgsUavCpuHandle_,
                                      drawArgsResource_.Get(),
                                      drawArgsClearValues, 0, nullptr);
    D3D12_RESOURCE_BARRIER clearBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(activeCountResource_.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(drawArgsResource_.Get()),
    };
    cmd->ResourceBarrier(_countof(clearBarriers), clearBarriers);

    std::vector<ParticleEmitterSettings> emitSettings;
    emitSettings.swap(pendingEmitSettings_);

    RecordUpdateDispatch(BuildEmitterForGPU(emitterSettings_, 0));

    D3D12_RESOURCE_BARRIER uavBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(particleResource_.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(freeListResource_.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(freeListIndexResource_.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(activeIndexResource_.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(activeCountResource_.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(drawArgsResource_.Get()),
    };
    cmd->ResourceBarrier(_countof(uavBarriers), uavBarriers);

    for (const ParticleEmitterSettings &settings : emitSettings) {
        RecordUpdateDispatch(BuildEmitterForGPU(settings, 1));
        cmd->ResourceBarrier(_countof(uavBarriers), uavBarriers);
    }

    D3D12_RESOURCE_BARRIER finalBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            particleResource_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(
            activeIndexResource_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(
            drawArgsResource_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
    };
    cmd->ResourceBarrier(_countof(finalBarriers), finalBarriers);
    activeIndexState_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    drawArgsState_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    updatePending_ = false;
}

void GPUParticleSystem::RecordUpdateDispatch(const EmitterForGPU &emitter) {
    auto *cmd = dxCommon_->GetCommandList();
    ConstantFrame *constantFrame = GetCurrentConstantFrame();
    if (cmd == nullptr || !updateRootSignature_ || !updatePSO_ ||
        constantFrame == nullptr ||
        !constantFrame->updateConstantBuffer ||
        constantFrame->mappedUpdateCB == nullptr) {
        return;
    }
    *constantFrame->mappedUpdateCB = updateConstants_;
    cmd->SetComputeRootSignature(updateRootSignature_.Get());
    cmd->SetPipelineState(updatePSO_.Get());
    cmd->SetComputeRootConstantBufferView(
        0, constantFrame->updateConstantBuffer->GetGPUVirtualAddress());
    static_assert(sizeof(EmitterForGPU) % sizeof(uint32_t) == 0);
    cmd->SetComputeRoot32BitConstants(
        1, static_cast<UINT>(sizeof(EmitterForGPU) / sizeof(uint32_t)),
        &emitter, 0);
    cmd->SetComputeRootDescriptorTable(2, particleUavGpuHandle_);
    cmd->SetComputeRootDescriptorTable(3, freeListUavGpuHandle_);
    cmd->SetComputeRootDescriptorTable(4, freeListIndexUavGpuHandle_);
    cmd->SetComputeRootDescriptorTable(5, activeIndexUavGpuHandle_);
    cmd->SetComputeRootDescriptorTable(6, activeCountUavGpuHandle_);
    cmd->SetComputeRootDescriptorTable(7, drawArgsUavGpuHandle_);
    cmd->Dispatch((maxParticles_ + kParticleThreadCount - 1u) /
                      kParticleThreadCount,
                  1, 1);
}

GPUParticleSystem::EmitterForGPU
GPUParticleSystem::BuildEmitterForGPU(const ParticleEmitterSettings &settings,
                                      uint32_t emit) const {
    EmitterForGPU emitter{};
    emitter.position = {settings.position.x, settings.position.y,
                        settings.position.z, 0.0f};
    emitter.spawnOffsetScale = {
        settings.spawnOffsetScale.x, settings.spawnOffsetScale.y,
        settings.spawnOffsetScale.z, settings.spawnShapeParams.x};
    emitter.basisRight = {settings.basisRight.x, settings.basisRight.y,
                          settings.basisRight.z, 0.0f};
    emitter.basisUp = {settings.basisUp.x, settings.basisUp.y,
                       settings.basisUp.z, 0.0f};
    emitter.basisForward = {settings.basisForward.x,
                            settings.basisForward.y,
                            settings.basisForward.z, 0.0f};
    emitter.directionAndDirectionalVelocity = {
        settings.direction.x, settings.direction.y, settings.direction.z,
        settings.directionalVelocity};
    emitter.velocityBiasAndRadialVelocity = {
        settings.velocityBias.x, settings.velocityBias.y, settings.velocityBias.z,
        settings.radialVelocity};
    emitter.lifeAndFade = {settings.baseLifeTime, settings.lifeTimeRandom,
                           settings.fadeInTime, settings.fadeOutTime};
    emitter.scale = {settings.startScale, settings.endScale,
                     settings.scaleRandom, settings.stretch};
    emitter.accelerationAndTurbulence = {
        settings.acceleration.x, settings.acceleration.y, settings.acceleration.z,
        settings.turbulence};
    emitter.motion = {settings.damping, settings.fadeOutPower,
                      static_cast<float>(settings.atlasColumns),
                      static_cast<float>(settings.atlasRows)};
    emitter.atlasAndRotation = {
        static_cast<float>(settings.atlasFrameStart),
        static_cast<float>(settings.atlasFrameCount), settings.rotationSpeed,
        settings.randomStartRotation ? 1.0f : 0.0f};
    emitter.tintColor = settings.tintColor;
    emitter.config = {
        static_cast<uint32_t>(settings.emissionType),
        static_cast<uint32_t>(settings.spawnShape),
        (std::min)({settings.burstCount, settings.maxParticles, maxParticles_}),
        emit};
    return emitter;
}

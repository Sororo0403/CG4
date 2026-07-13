#include "particle/GPUParticleSystem.h"

#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "internal/GPUParticleEmitterUtils.h"
#include "internal/GPUParticleSystemInternal.h"
#include "internal/GPUParticleSystemShared.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <new>
#include <numeric>
#include <random>

using namespace DirectX;

namespace {

using GpuParticleEmitterUtils::EstimateParticleActiveDuration;
using GpuParticleEmitterUtils::IsContinuousEmitter;
using GpuParticleEmitterUtils::NormalizeParticleEmitterSettings;
using GpuParticleEmitterUtils::ResolveTextureId;
using GpuParticleEmitterUtils::SanitizeFinite;
using GpuParticleSystemInternal::CheckedByteSize;
using GpuParticleSystemInternal::kMaxGpuParticles;
using GpuParticleSystemInternal::kMaxQueuedParticleEmitsPerFrame;
using GpuParticleSystemInternal::kParticleThreadCount;
using GpuParticleSystemInternal::kRequiredSrvDescriptors;
using GpuParticleSystemInternal::ParticleUploadPassScope;

constexpr float kParticleClearDeltaTime = 1.0e6f;

template <typename ResourceState>
bool HasRequiredParticleCoreResources(const ResourceState& resources) {
    const bool required[] = {
        static_cast<bool>(resources.particleResource),
        static_cast<bool>(resources.freeListResource),
        static_cast<bool>(resources.freeListIndexResource),
        static_cast<bool>(resources.activeIndexResource),
        static_cast<bool>(resources.activeCountResource),
        static_cast<bool>(resources.drawArgsResource),
    };
    return std::all_of(std::begin(required), std::end(required), [](bool value) { return value; });
}

template <typename ResourceState>
bool HasRequiredParticleGpuHandles(const ResourceState& resources) {
    const D3D12_GPU_DESCRIPTOR_HANDLE handles[] = {
        resources.particleSrvGpuHandle,    resources.particleUavGpuHandle,
        resources.freeListUavGpuHandle,    resources.freeListIndexUavGpuHandle,
        resources.activeIndexSrvGpuHandle, resources.activeIndexUavGpuHandle,
        resources.activeCountUavGpuHandle, resources.drawArgsUavGpuHandle,
    };
    return std::all_of(std::begin(handles), std::end(handles),
                       [](D3D12_GPU_DESCRIPTOR_HANDLE handle) { return handle.ptr != 0; });
}

template <typename Frames> bool HasRequiredExplicitSpawnFrames(const Frames& frames) {
    return !frames.empty() && std::all_of(frames.begin(), frames.end(), [](const auto& frame) {
        return frame.resource && frame.mappedSpawns != nullptr && frame.srvGpuHandle.ptr != 0 &&
               frame.capacity != 0u;
    });
}

} // namespace

GPUParticleSystem::GPUParticleSystem() : resources_(std::make_unique<ResourceState>()) {}

GPUParticleSystem::~GPUParticleSystem() {
    ReleaseResources(true);
}

class GPUParticleSystem::InitializationGuard {
public:
    explicit InitializationGuard(GPUParticleSystem& system) : system_(system) {}
    ~InitializationGuard() {
        if (active_) {
            std::deque<ParticleEmitterSettings> pendingEmitSettings;
            pendingEmitSettings.swap(system_.pendingEmitSettings_);
            std::vector<GPUParticleExplicitSpawn> pendingExplicitParticles;
            pendingExplicitParticles.swap(system_.pendingExplicitParticles_);
            system_.ReleaseResources(true);
            system_.pendingEmitSettings_ = std::move(pendingEmitSettings);
            system_.pendingExplicitParticles_ = std::move(pendingExplicitParticles);
        }
    }

    InitializationGuard(const InitializationGuard&) = delete;
    InitializationGuard& operator=(const InitializationGuard&) = delete;

    void Commit() {
        active_ = false;
    }

private:
    GPUParticleSystem& system_;
    bool active_ = true;
};

void GPUParticleSystem::ReleaseSharedResources() {
    GpuParticleShared::ReleaseDrawResources();
}

bool GPUParticleSystem::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager,
                                   TextureManager* textureManager, uint32_t textureId,
                                   uint32_t maxParticles) {
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager || !textureManager) {
        ReleaseResources(true);
        return false;
    }

    std::deque<ParticleEmitterSettings> pendingBeforeInitialize;
    pendingBeforeInitialize.swap(pendingEmitSettings_);
    std::vector<GPUParticleExplicitSpawn> pendingExplicitBeforeInitialize;
    pendingExplicitBeforeInitialize.swap(pendingExplicitParticles_);
    if (!ReleaseResources(true)) {
        pendingEmitSettings_ = std::move(pendingBeforeInitialize);
        pendingExplicitParticles_ = std::move(pendingExplicitBeforeInitialize);
        return false;
    }
    if (dxCommon->IsCommandListRecording() && !dxCommon->IsUploadPassActive()) {
        pendingEmitSettings_ = std::move(pendingBeforeInitialize);
        pendingExplicitParticles_ = std::move(pendingExplicitBeforeInitialize);
        return false;
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    textureManager_ = textureManager;
    InitializationGuard initializeGuard(*this);

    pendingExplicitParticles_ = std::move(pendingExplicitBeforeInitialize);
    if (!ConfigureInitialState(textureId, maxParticles, std::move(pendingBeforeInitialize))) {
        return false;
    }
    const std::vector<ParticleForGPU> particles = CreateInitialParticleData();
    if (!CreateInitializationGpuResources(particles)) {
        return false;
    }

    QueueInitialUpdateIfNeeded();
    initializeGuard.Commit();
    return true;
}

bool GPUParticleSystem::ConfigureInitialState(
    uint32_t textureId, uint32_t maxParticles,
    std::deque<ParticleEmitterSettings> pendingEmitSettings) {
    textureId_ = textureId;
    maxParticles_ = std::clamp(maxParticles, 1u, kMaxGpuParticles);
    pendingEmitSettings_ = std::move(pendingEmitSettings);
    if (CheckedByteSize(sizeof(ParticleForGPU), maxParticles_,
                        "GPUParticleSystem particle buffer size overflow") == 0 ||
        CheckedByteSize(sizeof(uint32_t), maxParticles_,
                        "GPUParticleSystem index buffer size overflow") == 0) {
        return false;
    }
    const UINT frameCount =
        dxCommon_ != nullptr ? (std::max)(1u, dxCommon_->GetSwapChainBufferCount()) : 1u;
    if (!srvManager_->CanAllocateDescriptors(kRequiredSrvDescriptors + frameCount)) {
        return false;
    }

    totalTime_ = 0.0f;
    emitterFrequencyTime_ = 0.0f;
    activeTimeRemaining_ = 0.0f;
    emitterSettings_ = NormalizeParticleEmitterSettings(ParticleEmitterSettings{});
    for (const ParticleEmitterSettings& settings : pendingEmitSettings_) {
        emitterSettings_ = settings;
        activeTimeRemaining_ =
            (std::max)(activeTimeRemaining_, EstimateParticleActiveDuration(settings));
    }
    activeTimeRemaining_ = std::accumulate(
        pendingExplicitParticles_.begin(), pendingExplicitParticles_.end(), activeTimeRemaining_,
        [](float activeDuration, const GPUParticleExplicitSpawn& particle) {
            return (std::max)(activeDuration, (std::max)(0.01f, particle.positionLife.w));
        });
    return true;
}

std::vector<GPUParticleSystem::ParticleForGPU> GPUParticleSystem::CreateInitialParticleData()
    const {
    std::mt19937 randomEngine{std::random_device{}()};
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    std::vector<ParticleForGPU> particles;
    try {
        particles.resize(maxParticles_);
    } catch (const std::exception&) {
        return {};
    }
    for (ParticleForGPU& particle : particles) {
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
        particle.params4 = {};
    }
    return particles;
}

bool GPUParticleSystem::CreateInitializationGpuResources(
    const std::vector<ParticleForGPU>& particles) {
    CreateRootSignatures();
    CreatePipelineStates();
    if (!resources_->updateRootSignature || !resources_->drawRootSignature ||
        !resources_->updatePso || !resources_->drawPso || !resources_->drawCommandSignature) {
        return false;
    }

    const bool ownsUploadPass = !dxCommon_->IsCommandListRecording();
    if (ownsUploadPass && !dxCommon_->BeginUpload()) {
        return false;
    }
    ParticleUploadPassScope uploadPass(dxCommon_, ownsUploadPass);
    if (!dxCommon_->IsCommandListRecording()) {
        return false;
    }
    CreateParticleBuffer(particles);
    CreateFreeListBuffers();
    CreateActiveDrawBuffers();
    if (!uploadPass.Finish()) {
        return false;
    }

    CreateConstantBuffers();
    return HasRequiredGpuResources();
}

bool GPUParticleSystem::HasRequiredGpuResources() const {
    return HasRequiredParticleCoreResources(*resources_) && HasConstantBuffers() &&
           HasRequiredParticleGpuHandles(*resources_) &&
           HasRequiredExplicitSpawnFrames(resources_->explicitSpawnFrames);
}

void GPUParticleSystem::QueueInitialUpdateIfNeeded() {
    if ((!pendingEmitSettings_.empty() || !pendingExplicitParticles_.empty()) &&
        HasConstantBuffers()) {
        resources_->updateConstants.time = {totalTime_, 0.0f, static_cast<float>(maxParticles_),
                                            0.0f};
        updatePending_ = true;
    }
}

void GPUParticleSystem::SetEmitterSettings(const ParticleEmitterSettings& settings) {
    ParticleEmitterSettings normalized = NormalizeParticleEmitterSettings(settings);
    const bool keepFrequencyTime =
        IsContinuousEmitter(emitterSettings_) && IsContinuousEmitter(normalized) &&
        std::abs(emitterSettings_.emitRate - normalized.emitRate) < 0.0001f &&
        emitterSettings_.burstCount == normalized.burstCount;
    emitterSettings_ = normalized;
    if (!keepFrequencyTime) {
        emitterFrequencyTime_ = 0.0f;
    }
}

void GPUParticleSystem::SetTextureFromFile(const std::wstring& filePath) {
    if (!textureManager_) {
        textureId_ = kInvalidResourceId;
        return;
    }

    textureId_ = textureManager_->Load(filePath);
}

void GPUParticleSystem::SetMaterialSettings(const GPUParticleMaterialSettings& settings) {
    materialSettings_ = settings;
    materialSettings_.params0 = SanitizeFinite(materialSettings_.params0, {});
    materialSettings_.params1 = SanitizeFinite(materialSettings_.params1, {});
    if (dxCommon_ && dxCommon_->GetDevice() && resources_->drawRootSignature) {
        const std::wstring pixelShaderPath = materialSettings_.pixelShaderPath.empty()
                                                 ? std::wstring(ShaderPaths::ParticlePS)
                                                 : materialSettings_.pixelShaderPath;
        if (ID3D12PipelineState* drawPso = GpuParticleShared::GetOrCreateDrawPipeline(
                dxCommon_->GetDevice(), resources_->drawRootSignature.Get(), pixelShaderPath,
                materialSettings_.blendMode)) {
            resources_->drawPso = drawPso;
        }
    }
}
void GPUParticleSystem::EmitOnce(const ParticleEmitterSettings& settings) {
    ParticleEmitterSettings normalized = NormalizeParticleEmitterSettings(settings);
    emitterSettings_ = normalized;
    emitterFrequencyTime_ = 0.0f;
    try {
        pendingEmitSettings_.push_back(normalized);
    } catch (const std::exception&) {
        return;
    }
    if (pendingEmitSettings_.size() > kMaxQueuedParticleEmitsPerFrame) {
        pendingEmitSettings_.pop_front();
    }
    activeTimeRemaining_ =
        (std::max)(activeTimeRemaining_, EstimateParticleActiveDuration(normalized));
    if (HasConstantBuffers() && !updatePending_) {
        resources_->updateConstants.time = {totalTime_, 0.0f, static_cast<float>(maxParticles_),
                                            0.0f};
        updatePending_ = true;
    }
}

size_t GPUParticleSystem::EmitParticles(const std::vector<GPUParticleExplicitSpawn>& particles) {
    if (particles.empty()) {
        return 0u;
    }

    const size_t capacityLimit = maxParticles_ != 0u ? static_cast<size_t>(maxParticles_)
                                                     : static_cast<size_t>(kMaxGpuParticles);
    const size_t appendCount = (std::min)(particles.size(), capacityLimit);
    if (appendCount == 0u) {
        return 0u;
    }

    try {
        std::vector<GPUParticleExplicitSpawn> updated = pendingExplicitParticles_;
        const size_t totalCount = updated.size() + appendCount;
        if (totalCount > capacityLimit) {
            const size_t eraseCount = (std::min)(updated.size(), totalCount - capacityLimit);
            if (eraseCount >= updated.size()) {
                updated.clear();
            } else if (eraseCount > 0u) {
                updated.erase(updated.begin(),
                              updated.begin() + static_cast<std::ptrdiff_t>(eraseCount));
            }
        }
        updated.insert(updated.end(), particles.begin(),
                       particles.begin() + static_cast<std::ptrdiff_t>(appendCount));
        pendingExplicitParticles_.swap(updated);
    } catch (const std::exception&) {
        return 0u;
    }

    const float maxLifeTime = std::accumulate(
        particles.begin(), particles.begin() + static_cast<std::ptrdiff_t>(appendCount), 0.01f,
        [](float maxLife, const GPUParticleExplicitSpawn& particle) {
            return (std::max)(maxLife, (std::max)(0.01f, particle.positionLife.w));
        });
    activeTimeRemaining_ = (std::max)(activeTimeRemaining_, maxLifeTime);

    if (HasConstantBuffers() && !updatePending_) {
        resources_->updateConstants.time = {totalTime_, 0.0f, static_cast<float>(maxParticles_),
                                            0.0f};
        updatePending_ = true;
    }
    return appendCount;
}

GPUParticleSystem::ConstantFrame* GPUParticleSystem::GetCurrentConstantFrame() {
    if (resources_->constantFrames.empty()) {
        return nullptr;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr ? dxCommon_->GetBackBufferIndex() % resources_->constantFrames.size()
                             : 0;
    return &resources_->constantFrames[frameIndex];
}

const GPUParticleSystem::ConstantFrame* GPUParticleSystem::GetCurrentConstantFrame() const {
    if (resources_->constantFrames.empty()) {
        return nullptr;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr ? dxCommon_->GetBackBufferIndex() % resources_->constantFrames.size()
                             : 0;
    return &resources_->constantFrames[frameIndex];
}

bool GPUParticleSystem::HasConstantBuffers() const {
    if (resources_->constantFrames.empty()) {
        return false;
    }
    return std::all_of(resources_->constantFrames.begin(), resources_->constantFrames.end(),
                       [](const ConstantFrame& frame) {
                           return frame.updateConstantBuffer && frame.drawConstantBuffer &&
                                  frame.mappedUpdateCB != nullptr && frame.mappedDrawCB != nullptr;
                       });
}

void GPUParticleSystem::Clear() {
    pendingEmitSettings_.clear();
    pendingExplicitParticles_.clear();
    activeTimeRemaining_ = 0.0f;
    emitterFrequencyTime_ = 0.0f;

    if (!HasConstantBuffers() || maxParticles_ == 0) {
        updatePending_ = false;
        clearPending_ = false;
        return;
    }

    resources_->updateConstants.time = {totalTime_, kParticleClearDeltaTime,
                                        static_cast<float>(maxParticles_), 0.0f};
    updatePending_ = true;
    clearPending_ = true;
    if (dxCommon_ && dxCommon_->IsCommandListRecording()) {
        DispatchUpdate();
    }
}

void GPUParticleSystem::Update(float deltaTime) {
    deltaTime = std::clamp(SanitizeFinite(deltaTime, 0.0f), 0.0f, 0.1f);
    totalTime_ += deltaTime;

    if (!HasConstantBuffers()) {
        return;
    }

    if (clearPending_) {
        if (dxCommon_ && dxCommon_->IsCommandListRecording()) {
            DispatchUpdate();
        }
        if (clearPending_) {
            return;
        }
    }

    const bool continuousEmitter = IsContinuousEmitter(emitterSettings_);
    const bool wasActive = activeTimeRemaining_ > 0.0f;

    if (wasActive) {
        activeTimeRemaining_ = (std::max)(0.0f, activeTimeRemaining_ - deltaTime);
    }

    if (continuousEmitter) {
        emitterFrequencyTime_ += deltaTime;
        const float safeEmitRate =
            (std::max)(SanitizeFinite(emitterSettings_.emitRate, 0.0f), 0.0001f);
        const float interval = 1.0f / safeEmitRate;
        while (emitterFrequencyTime_ >= interval &&
               pendingEmitSettings_.size() < kMaxQueuedParticleEmitsPerFrame) {
            emitterFrequencyTime_ -= interval;
            try {
                pendingEmitSettings_.push_back(emitterSettings_);
            } catch (const std::exception&) {
                break;
            }
            activeTimeRemaining_ =
                (std::max)(activeTimeRemaining_, EstimateParticleActiveDuration(emitterSettings_));
        }
        if (emitterFrequencyTime_ >= interval) {
            emitterFrequencyTime_ = std::fmod(emitterFrequencyTime_, interval);
        }
    }

    if (pendingEmitSettings_.empty() && pendingExplicitParticles_.empty() && !continuousEmitter &&
        !wasActive) {
        return;
    }

    resources_->updateConstants.time = {totalTime_, deltaTime, static_cast<float>(maxParticles_),
                                        0.0f};

    updatePending_ = true;
    if (dxCommon_ && dxCommon_->IsCommandListRecording()) {
        DispatchUpdate();
    }
}

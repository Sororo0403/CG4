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
using GpuParticleEmitterUtils::ClampColor;
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

float NextMeshRandom(uint32_t& state) {
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return static_cast<float>(state & 0x00FFFFFFu) / 16777216.0f;
}

float TriangleArea(const ParticleMeshTriangle& triangle) {
    const XMVECTOR a = XMLoadFloat3(&triangle.a);
    const XMVECTOR ab = XMVectorSubtract(XMLoadFloat3(&triangle.b), a);
    const XMVECTOR ac = XMVectorSubtract(XMLoadFloat3(&triangle.c), a);
    return 0.5f * XMVectorGetX(XMVector3Length(XMVector3Cross(ab, ac)));
}

XMFLOAT3 SampleTriangle(const ParticleMeshTriangle& triangle, float u, float v) {
    const float root = std::sqrt(u);
    const float wa = 1.0f - root;
    const float wb = root * (1.0f - v);
    const float wc = root * v;
    return {triangle.a.x * wa + triangle.b.x * wb + triangle.c.x * wc,
            triangle.a.y * wa + triangle.b.y * wb + triangle.c.y * wc,
            triangle.a.z * wa + triangle.b.z * wb + triangle.c.z * wc};
}

const ParticleMeshTriangle* SelectMeshTriangle(const ParticleEmitterSettings& settings,
                                               float selection) {
    const float totalArea = std::accumulate(
        settings.meshTriangles.begin(), settings.meshTriangles.end(), 0.0f,
        [](float sum, const ParticleMeshTriangle& triangle) {
            return sum + TriangleArea(triangle);
        });
    if (totalArea <= 0.000001f) {
        return nullptr;
    }

    const float target = selection * totalArea;
    float accumulated = 0.0f;
    for (const ParticleMeshTriangle& triangle : settings.meshTriangles) {
        accumulated += TriangleArea(triangle);
        if (target <= accumulated) {
            return &triangle;
        }
    }
    return &settings.meshTriangles.back();
}

XMFLOAT3 TransformMeshPoint(const ParticleEmitterSettings& settings, const XMFLOAT3& point) {
    const XMFLOAT3 scaled{point.x * settings.spawnOffsetScale.x,
                          point.y * settings.spawnOffsetScale.y,
                          point.z * settings.spawnOffsetScale.z};
    return {settings.position.x + settings.basisRight.x * scaled.x +
                settings.basisUp.x * scaled.y + settings.basisForward.x * scaled.z,
            settings.position.y + settings.basisRight.y * scaled.x +
                settings.basisUp.y * scaled.y + settings.basisForward.y * scaled.z,
            settings.position.z + settings.basisRight.z * scaled.x +
                settings.basisUp.z * scaled.y + settings.basisForward.z * scaled.z};
}

float PackParticleLight(const ParticleEmitterSettings& settings) {
    const uint32_t assignment = settings.assignedLight < 4u ? settings.assignedLight + 1u : 0u;
    return static_cast<float>(assignment * 2u) + settings.lightInfluence;
}

GPUParticleExplicitSpawn MakeMeshSpawn(const ParticleEmitterSettings& settings,
                                       const XMFLOAT3& worldPosition, float lifeRandom,
                                       float scaleRandom, float atlasRandom) {
    const XMVECTOR radial = XMVector3Normalize(XMVectorSubtract(XMLoadFloat3(&worldPosition),
                                                               XMLoadFloat3(&settings.position)));
    const XMVECTOR direction = XMVector3Normalize(XMLoadFloat3(&settings.direction));
    XMFLOAT3 velocity{};
    XMStoreFloat3(&velocity, XMVectorAdd(
                                XMVectorScale(radial, settings.radialVelocity),
                                XMVectorAdd(XMVectorScale(direction, settings.directionalVelocity),
                                            XMLoadFloat3(&settings.velocityBias))));

    const uint32_t frameCount = (std::max)(1u, settings.atlasFrameCount);
    const uint32_t frame = settings.atlasFrameStart +
                           (std::min)(static_cast<uint32_t>(atlasRandom * frameCount),
                                      frameCount - 1u);
    GPUParticleExplicitSpawn spawn{};
    spawn.positionLife = {worldPosition.x, worldPosition.y, worldPosition.z,
                          settings.baseLifeTime + lifeRandom * settings.lifeTimeRandom};
    spawn.velocityStartScale = {velocity.x, velocity.y, velocity.z,
                                settings.startScale + scaleRandom * settings.scaleRandom};
    spawn.color = settings.tintColor;
    spawn.scaleFade = {settings.endScale, settings.fadeInTime, settings.fadeOutTime,
                       settings.fadeOutPower};
    spawn.motion = {settings.stretch, settings.damping, settings.turbulence,
                    settings.rotationSpeed};
    spawn.accelerationAtlas = {settings.acceleration.x, settings.acceleration.y,
                               settings.acceleration.z, static_cast<float>(frame)};
    spawn.drawAxis = {settings.basisUp.x, settings.basisUp.y, settings.basisUp.z,
                      PackParticleLight(settings)};
    spawn.atlas = {settings.atlasColumns, settings.atlasRows,
                   settings.randomStartRotation ? 1u : 0u, 0u};
    return spawn;
}

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

void GPUParticleSystem::SetLightingSettings(const GPUParticleLightingSettings& settings) {
    lightingSettings_ = settings;
    lightingSettings_.direction =
        SanitizeFinite(lightingSettings_.direction, {-0.35f, -0.95f, 0.25f});
    lightingSettings_.intensity =
        (std::max)(0.0f, SanitizeFinite(lightingSettings_.intensity, 1.0f));
    lightingSettings_.color = ClampColor(lightingSettings_.color, {1.0f, 0.96f, 0.9f, 1.0f});
    lightingSettings_.ambient =
        ClampColor(lightingSettings_.ambient, {0.2f, 0.22f, 0.26f, 1.0f});
    lightingSettings_.pointLightCount =
        (std::min)(lightingSettings_.pointLightCount,
                   static_cast<uint32_t>(lightingSettings_.pointLights.size()));
    for (GPUParticleLightingSettings::PointLight& light : lightingSettings_.pointLights) {
        light.position = SanitizeFinite(light.position, {0.0f, 0.0f, 0.0f});
        light.range = (std::max)(0.001f, SanitizeFinite(light.range, 1.0f));
        light.color = ClampColor(light.color, {1.0f, 1.0f, 1.0f, 1.0f});
        light.intensity = (std::max)(0.0f, SanitizeFinite(light.intensity, 1.0f));
    }
}
void GPUParticleSystem::EmitOnce(const ParticleEmitterSettings& settings) {
    ParticleEmitterSettings normalized = NormalizeParticleEmitterSettings(settings);
    emitterSettings_ = normalized;
    emitterFrequencyTime_ = 0.0f;
    if (normalized.spawnShape == ParticleSpawnShape::Mesh) {
        QueueMeshSurfaceEmission(normalized);
        return;
    }
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

void GPUParticleSystem::QueueMeshSurfaceEmission(const ParticleEmitterSettings& settings) {
    const uint32_t emitCount = (std::min)({settings.burstCount, settings.maxParticles,
                                           maxParticles_});
    std::vector<GPUParticleExplicitSpawn> particles;
    particles.reserve(emitCount);
    for (uint32_t i = 0; i < emitCount; ++i) {
        const ParticleMeshTriangle* triangle =
            SelectMeshTriangle(settings, NextMeshRandom(meshRandomState_));
        if (triangle == nullptr) {
            break;
        }
        const XMFLOAT3 localPoint = SampleTriangle(
            *triangle, NextMeshRandom(meshRandomState_), NextMeshRandom(meshRandomState_));
        const XMFLOAT3 worldPoint = TransformMeshPoint(settings, localPoint);
        particles.push_back(MakeMeshSpawn(settings, worldPoint, NextMeshRandom(meshRandomState_),
                                          NextMeshRandom(meshRandomState_),
                                          NextMeshRandom(meshRandomState_)));
    }
    EmitParticles(particles);
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

uint32_t GPUParticleSystem::AddEmitter(const ParticleEmitterSettings& settings, bool enabled) {
    ManagedEmitter emitter{};
    emitter.id = nextEmitterId_++;
    if (emitter.id == kInvalidParticleEmitterId) {
        emitter.id = nextEmitterId_++;
    }
    emitter.settings = NormalizeParticleEmitterSettings(settings);
    emitter.enabled = enabled;
    try {
        managedEmitters_.push_back(emitter);
    } catch (const std::exception&) {
        return kInvalidParticleEmitterId;
    }
    return emitter.id;
}

bool GPUParticleSystem::UpdateEmitter(uint32_t emitterId,
                                      const ParticleEmitterSettings& settings) {
    const auto emitter = std::ranges::find_if(
        managedEmitters_, [emitterId](const ManagedEmitter& item) { return item.id == emitterId; });
    if (emitter == managedEmitters_.end()) {
        return false;
    }
    emitter->settings = NormalizeParticleEmitterSettings(settings);
    return true;
}

bool GPUParticleSystem::RemoveEmitter(uint32_t emitterId) {
    const auto emitter = std::ranges::find_if(
        managedEmitters_, [emitterId](const ManagedEmitter& item) { return item.id == emitterId; });
    if (emitter == managedEmitters_.end()) {
        return false;
    }
    managedEmitters_.erase(emitter);
    return true;
}

bool GPUParticleSystem::SetEmitterEnabled(uint32_t emitterId, bool enabled) {
    const auto emitter = std::ranges::find_if(
        managedEmitters_, [emitterId](const ManagedEmitter& item) { return item.id == emitterId; });
    if (emitter == managedEmitters_.end()) {
        return false;
    }
    emitter->enabled = enabled;
    return true;
}

bool GPUParticleSystem::GetEmitterSettings(uint32_t emitterId,
                                           ParticleEmitterSettings& settings) const {
    const auto emitter = std::ranges::find_if(
        managedEmitters_, [emitterId](const ManagedEmitter& item) { return item.id == emitterId; });
    if (emitter == managedEmitters_.end()) {
        return false;
    }
    settings = emitter->settings;
    return true;
}

void GPUParticleSystem::QueueContinuousEmitter(const ParticleEmitterSettings& settings,
                                               float deltaTime, float& frequencyTime) {
    if (!IsContinuousEmitter(settings)) {
        return;
    }
    frequencyTime += deltaTime;
    const float emitRate = (std::max)(settings.emitRate, 0.0001f);
    const float interval = 1.0f / emitRate;
    size_t queuedCount = 0u;
    while (frequencyTime >= interval && queuedCount < kMaxQueuedParticleEmitsPerFrame) {
        frequencyTime -= interval;
        if (settings.spawnShape == ParticleSpawnShape::Mesh) {
            QueueMeshSurfaceEmission(settings);
        } else {
            try {
                pendingEmitSettings_.push_back(settings);
            } catch (const std::exception&) {
                break;
            }
        }
        ++queuedCount;
        activeTimeRemaining_ =
            (std::max)(activeTimeRemaining_, EstimateParticleActiveDuration(settings));
    }
    if (frequencyTime >= interval) {
        frequencyTime = std::fmod(frequencyTime, interval);
    }
}

void GPUParticleSystem::UpdateManagedEmitters(float deltaTime) {
    for (ManagedEmitter& emitter : managedEmitters_) {
        if (emitter.enabled) {
            QueueContinuousEmitter(emitter.settings, deltaTime, emitter.frequencyTime);
        }
    }
}

void GPUParticleSystem::SetFields(const std::vector<ParticleFieldSettings>& fields) {
    constexpr size_t kMaxFields = 8u;
    std::vector<ParticleFieldSettings> normalized;
    try {
        normalized.reserve((std::min)(fields.size(), kMaxFields));
        for (const ParticleFieldSettings& source : fields) {
            if (normalized.size() >= kMaxFields) {
                break;
            }
            ParticleFieldSettings field = source;
            field.position = SanitizeFinite(field.position, {});
            field.direction = SanitizeFinite(field.direction, {0.0f, 1.0f, 0.0f});
            field.radius = (std::max)(0.001f, SanitizeFinite(field.radius, 2.0f));
            field.strength = SanitizeFinite(field.strength, 0.0f);
            field.falloff = (std::max)(0.01f, SanitizeFinite(field.falloff, 1.0f));
            normalized.push_back(field);
        }
    } catch (const std::exception&) {
        return;
    }
    fields_.swap(normalized);
    UpdateFieldConstants();
}

void GPUParticleSystem::UpdateFieldConstants() {
    resources_->updateConstants.fields = {};
    const size_t count = (std::min)(fields_.size(), resources_->updateConstants.fields.size());
    for (size_t i = 0; i < count; ++i) {
        const ParticleFieldSettings& source = fields_[i];
        UpdateConstantBufferData::FieldForGPU& target = resources_->updateConstants.fields[i];
        target.positionRadius = {source.position.x, source.position.y, source.position.z,
                                 source.radius};
        target.directionStrength = {source.direction.x, source.direction.y, source.direction.z,
                                    source.strength};
        target.params = {static_cast<float>(source.type), source.falloff,
                         source.enabled ? 1.0f : 0.0f, 0.0f};
    }
    resources_->updateConstants.fieldConfig = {static_cast<uint32_t>(count), 0u, 0u, 0u};
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
    UpdateFieldConstants();

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

    QueueContinuousEmitter(emitterSettings_, deltaTime, emitterFrequencyTime_);
    UpdateManagedEmitters(deltaTime);

    const bool managedContinuousEmitter = std::ranges::any_of(
        managedEmitters_, [](const ManagedEmitter& emitter) {
            return emitter.enabled && IsContinuousEmitter(emitter.settings);
        });
    if (pendingEmitSettings_.empty() && pendingExplicitParticles_.empty() &&
        !continuousEmitter && !managedContinuousEmitter && !wasActive) {
        return;
    }

    resources_->updateConstants.time = {totalTime_, deltaTime, static_cast<float>(maxParticles_),
                                        0.0f};

    updatePending_ = true;
    if (dxCommon_ && dxCommon_->IsCommandListRecording()) {
        DispatchUpdate();
    }
}

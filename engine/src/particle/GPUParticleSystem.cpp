#include "particle/GPUParticleSystem.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <map>
#include <random>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {

constexpr uint32_t kParticleThreadCount = 256u;
constexpr size_t kMaxQueuedParticleEmitsPerFrame = 128u;
constexpr uint32_t kMaxGpuParticles = 1'048'576u;
constexpr UINT kRequiredSrvDescriptors = 8u;

ID3D12Device *gCachedParticleDrawDevice = nullptr;
ComPtr<ID3D12RootSignature> gCachedParticleDrawRootSignature;
ComPtr<ID3D12CommandSignature> gCachedParticleDrawCommandSignature;
std::map<std::wstring, ComPtr<ID3D12PipelineState>> gParticleDrawPsoCache;

float EstimateParticleActiveDuration(const ParticleEmitterSettings &settings) {
    return (std::max)(0.0f, settings.baseLifeTime + settings.lifeTimeRandom +
                                settings.fadeOutTime);
}

bool IsContinuousEmitter(const ParticleEmitterSettings &settings) {
    return settings.emissionType == ParticleEmissionType::Continuous &&
           settings.emitRate > 0.0f;
}

float SanitizeFinite(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

XMFLOAT3 SanitizeFinite(XMFLOAT3 value, XMFLOAT3 fallback) {
    value.x = SanitizeFinite(value.x, fallback.x);
    value.y = SanitizeFinite(value.y, fallback.y);
    value.z = SanitizeFinite(value.z, fallback.z);
    return value;
}

XMFLOAT4 SanitizeFinite(XMFLOAT4 value, XMFLOAT4 fallback) {
    value.x = SanitizeFinite(value.x, fallback.x);
    value.y = SanitizeFinite(value.y, fallback.y);
    value.z = SanitizeFinite(value.z, fallback.z);
    value.w = SanitizeFinite(value.w, fallback.w);
    return value;
}

XMFLOAT4 ClampColor(XMFLOAT4 value, XMFLOAT4 fallback) {
    value = SanitizeFinite(value, fallback);
    value.x = std::clamp(value.x, 0.0f, 1.0f);
    value.y = std::clamp(value.y, 0.0f, 1.0f);
    value.z = std::clamp(value.z, 0.0f, 1.0f);
    value.w = std::clamp(value.w, 0.0f, 1.0f);
    return value;
}

uint32_t ResolveTextureId(TextureManager *textureManager, uint32_t textureId,
                          uint32_t fallbackTextureId) {
    if (textureManager == nullptr) {
        return UINT32_MAX;
    }
    if (textureId != UINT32_MAX &&
        textureManager->IsValidTextureId(textureId)) {
        return textureId;
    }
    if (fallbackTextureId != UINT32_MAX &&
        textureManager->IsValidTextureId(fallbackTextureId)) {
        return fallbackTextureId;
    }
    return textureManager->GetWhiteTextureId();
}

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

UINT Align256(size_t size) {
    if (size > static_cast<size_t>((std::numeric_limits<UINT>::max)()) - 0xFFu) {
        return 0;
    }
    return static_cast<UINT>((size + 0xFFu) & ~size_t{0xFFu});
}

bool CreateCommittedResourceChecked(
    ID3D12Device *device, const D3D12_HEAP_PROPERTIES *heapProperties,
    D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *resourceDesc,
    D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *clearValue,
    ID3D12Resource **resource) {
    if (device == nullptr || heapProperties == nullptr ||
        resourceDesc == nullptr || resource == nullptr) {
        return false;
    }
    *resource = nullptr;
    return SUCCEEDED(device->CreateCommittedResource(
        heapProperties, heapFlags, resourceDesc, initialState, clearValue,
        IID_PPV_ARGS(resource))) &&
           *resource != nullptr;
}

bool MapResourceChecked(ID3D12Resource *resource, void **mapped) {
    if (resource == nullptr || mapped == nullptr) {
        return false;
    }
    *mapped = nullptr;
    return SUCCEEDED(resource->Map(0, nullptr, mapped)) && *mapped != nullptr;
}

bool AllocateSrvHandles(SrvManager *srvManager, uint32_t &index,
                        D3D12_CPU_DESCRIPTOR_HANDLE &cpuHandle,
                        D3D12_GPU_DESCRIPTOR_HANDLE &gpuHandle) {
    index = UINT32_MAX;
    cpuHandle = {};
    gpuHandle = {};

    if (srvManager == nullptr || !srvManager->CanAllocate()) {
        return false;
    }

    index = srvManager->Allocate();
    if (index == UINT32_MAX) {
        return false;
    }

    cpuHandle = srvManager->GetCpuHandle(index);
    gpuHandle = srvManager->GetGpuHandle(index);
    if (cpuHandle.ptr == 0 || gpuHandle.ptr == 0) {
        srvManager->FreeIfAllocated(index);
        index = UINT32_MAX;
        cpuHandle = {};
        gpuHandle = {};
        return false;
    }

    return true;
}

ParticleEmitterSettings
NormalizeParticleEmitterSettings(ParticleEmitterSettings settings) {
    settings.maxParticles = (std::max)(1u, settings.maxParticles);
    settings.emitRate = (std::max)(0.0f, settings.emitRate);
    settings.burstCount = (std::max)(1u, settings.burstCount);
    settings.position = SanitizeFinite(settings.position, {0.0f, 0.0f, 0.0f});
    settings.spawnOffsetScale =
        SanitizeFinite(settings.spawnOffsetScale, {0.0f, 0.0f, 0.0f});
    settings.spawnShapeParams =
        SanitizeFinite(settings.spawnShapeParams, {0.0f, 0.0f, 0.0f, 0.0f});
    settings.tintColor =
        SanitizeFinite(settings.tintColor, {1.0f, 1.0f, 1.0f, 1.0f});
    settings.direction = SanitizeFinite(settings.direction, {0.0f, 1.0f, 0.0f});
    settings.basisRight =
        SanitizeFinite(settings.basisRight, {1.0f, 0.0f, 0.0f});
    settings.basisUp = SanitizeFinite(settings.basisUp, {0.0f, 1.0f, 0.0f});
    settings.basisForward =
        SanitizeFinite(settings.basisForward, {0.0f, 0.0f, 1.0f});
    settings.velocityBias =
        SanitizeFinite(settings.velocityBias, {0.0f, 0.0f, 0.0f});
    settings.emitRate = SanitizeFinite(settings.emitRate, 0.0f);
    settings.radialVelocity = SanitizeFinite(settings.radialVelocity, 0.0f);
    settings.directionalVelocity =
        SanitizeFinite(settings.directionalVelocity, 0.0f);
    settings.baseLifeTime = SanitizeFinite(settings.baseLifeTime, 0.01f);
    settings.lifeTimeRandom = SanitizeFinite(settings.lifeTimeRandom, 0.0f);
    settings.startScale = SanitizeFinite(settings.startScale, 0.001f);
    settings.endScale = SanitizeFinite(settings.endScale, 0.0f);
    settings.scaleRandom = SanitizeFinite(settings.scaleRandom, 0.0f);
    settings.stretch = SanitizeFinite(settings.stretch, 0.0f);
    settings.turbulence = SanitizeFinite(settings.turbulence, 0.0f);
    settings.damping = SanitizeFinite(settings.damping, 1.0f);
    settings.fadeInTime = SanitizeFinite(settings.fadeInTime, 0.0f);
    settings.fadeOutTime = SanitizeFinite(settings.fadeOutTime, 0.0f);
    settings.fadeOutPower = SanitizeFinite(settings.fadeOutPower, 1.0f);
    settings.rotationSpeed = SanitizeFinite(settings.rotationSpeed, 0.0f);
    settings.spawnOffsetScale.x = (std::max)(0.0f, settings.spawnOffsetScale.x);
    settings.spawnOffsetScale.y = (std::max)(0.0f, settings.spawnOffsetScale.y);
    settings.spawnOffsetScale.z = (std::max)(0.0f, settings.spawnOffsetScale.z);
    settings.spawnShapeParams.x = (std::max)(0.0f, settings.spawnShapeParams.x);
    settings.radialVelocity = (std::max)(0.0f, settings.radialVelocity);
    settings.directionalVelocity =
        (std::max)(0.0f, settings.directionalVelocity);
    settings.baseLifeTime = (std::max)(0.01f, settings.baseLifeTime);
    settings.lifeTimeRandom = (std::max)(0.0f, settings.lifeTimeRandom);
    settings.startScale = (std::max)(0.001f, settings.startScale);
    settings.endScale = (std::max)(0.0f, settings.endScale);
    settings.scaleRandom = (std::max)(0.0f, settings.scaleRandom);
    settings.stretch = (std::max)(0.0f, settings.stretch);
    settings.atlasColumns = (std::max)(1u, settings.atlasColumns);
    settings.atlasRows = (std::max)(1u, settings.atlasRows);
    if (settings.atlasColumns > UINT32_MAX / settings.atlasRows) {
        settings.atlasColumns = 1u;
        settings.atlasRows = 1u;
    }
    const uint32_t atlasFrameCapacity =
        settings.atlasColumns * settings.atlasRows;
    settings.atlasFrameStart =
        (std::min)(settings.atlasFrameStart, atlasFrameCapacity - 1u);
    settings.atlasFrameCount =
        (std::clamp)(settings.atlasFrameCount, 1u,
                     atlasFrameCapacity - settings.atlasFrameStart);
    settings.turbulence = (std::max)(0.0f, settings.turbulence);
    settings.damping = (std::clamp)(settings.damping, 0.0f, 1.0f);
    settings.fadeInTime = (std::max)(0.0f, settings.fadeInTime);
    settings.fadeOutTime = (std::max)(0.0f, settings.fadeOutTime);
    settings.fadeOutPower = (std::max)(0.01f, settings.fadeOutPower);
    settings.tintColor.w = (std::clamp)(settings.tintColor.w, 0.0f, 1.0f);
    return settings;
}

void ResetParticleDrawCacheIfDeviceChanged(ID3D12Device *device) {
    if (gCachedParticleDrawDevice == device) {
        return;
    }

    gCachedParticleDrawDevice = device;
    gCachedParticleDrawRootSignature.Reset();
    gCachedParticleDrawCommandSignature.Reset();
    gParticleDrawPsoCache.clear();
}

ID3D12RootSignature *GetSharedParticleDrawRootSignature(ID3D12Device *device) {
    if (device == nullptr) {
        return nullptr;
    }
    ResetParticleDrawCacheIfDeviceChanged(device);
    if (gCachedParticleDrawRootSignature) {
        return gCachedParticleDrawRootSignature.Get();
    }

    CD3DX12_ROOT_PARAMETER params[5]{};
    params[0].InitAsConstantBufferView(0);

    CD3DX12_DESCRIPTOR_RANGE particleRange{};
    particleRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &particleRange);

    CD3DX12_DESCRIPTOR_RANGE textureRange{};
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    params[2].InitAsDescriptorTable(1, &textureRange);

    CD3DX12_DESCRIPTOR_RANGE noiseTextureRange{};
    noiseTextureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
    params[3].InitAsDescriptorTable(1, &noiseTextureRange);

    CD3DX12_DESCRIPTOR_RANGE activeIndexRange{};
    activeIndexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
    params[4].InitAsDescriptorTable(1, &activeIndexRange);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> blob, error;
    if (FAILED(D3D12SerializeRootSignature(
            &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        !blob) {
        return nullptr;
    }
    if (FAILED(device->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&gCachedParticleDrawRootSignature))) ||
        !gCachedParticleDrawRootSignature) {
        return nullptr;
    }
    return gCachedParticleDrawRootSignature.Get();
}

ID3D12PipelineState *GetOrCreateParticleDrawPso(
    ID3D12Device *device, ID3D12RootSignature *rootSignature,
    const std::wstring &pixelShaderPath) {
    if (device == nullptr || rootSignature == nullptr) {
        return nullptr;
    }
    ResetParticleDrawCacheIfDeviceChanged(device);

    auto found = gParticleDrawPsoCache.find(pixelShaderPath);
    if (found != gParticleDrawPsoCache.end()) {
        return found->second.Get();
    }

    auto vs =
        ShaderCompiler::Compile(ShaderPaths::ParticleVS, "main", "vs_6_6");
    auto ps = ShaderCompiler::Compile(pixelShaderPath, "main", "ps_6_6");
    if (!vs || !ps) {
        return nullptr;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC drawPso{};
    drawPso.pRootSignature = rootSignature;
    drawPso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    drawPso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    drawPso.InputLayout = {nullptr, 0};
    drawPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    drawPso.NumRenderTargets = 1;
    drawPso.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
    drawPso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    drawPso.SampleDesc.Count = 1;
    drawPso.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    drawPso.RasterizerState = rasterizer;

    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    drawPso.BlendState = blend;

    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    drawPso.DepthStencilState = depth;

    ComPtr<ID3D12PipelineState> pso;
    if (FAILED(device->CreateGraphicsPipelineState(
            &drawPso, IID_PPV_ARGS(&pso))) ||
        !pso) {
        return nullptr;
    }

    ID3D12PipelineState *result = pso.Get();
    gParticleDrawPsoCache[pixelShaderPath] = std::move(pso);
    return result;
}

ID3D12CommandSignature *GetSharedParticleDrawCommandSignature(
    ID3D12Device *device) {
    if (device == nullptr) {
        return nullptr;
    }
    ResetParticleDrawCacheIfDeviceChanged(device);
    if (gCachedParticleDrawCommandSignature) {
        return gCachedParticleDrawCommandSignature.Get();
    }

    D3D12_INDIRECT_ARGUMENT_DESC indirectArgument{};
    indirectArgument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

    D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc{};
    commandSignatureDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    commandSignatureDesc.NumArgumentDescs = 1;
    commandSignatureDesc.pArgumentDescs = &indirectArgument;
    if (FAILED(device->CreateCommandSignature(
            &commandSignatureDesc, nullptr,
            IID_PPV_ARGS(&gCachedParticleDrawCommandSignature))) ||
        !gCachedParticleDrawCommandSignature) {
        return nullptr;
    }
    return gCachedParticleDrawCommandSignature.Get();
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

    void Finish() {
        if (!active_) {
            return;
        }
        dxCommon_->EndUpload();
        active_ = false;
    }

  private:
    DirectXCommon *dxCommon_ = nullptr;
    bool active_ = false;
};

}

GPUParticleSystem::~GPUParticleSystem() {
    ReleaseResources();
}

class GPUParticleSystem::InitializationGuard {
  public:
    explicit InitializationGuard(GPUParticleSystem &system) : system_(system) {}
    ~InitializationGuard() {
        if (active_) {
            system_.ReleaseResources();
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
    gParticleDrawPsoCache.clear();
    gCachedParticleDrawCommandSignature.Reset();
    gCachedParticleDrawRootSignature.Reset();
    gCachedParticleDrawDevice = nullptr;
}

void GPUParticleSystem::Initialize(DirectXCommon *dxCommon,
                                   SrvManager *srvManager,
                                   TextureManager *textureManager,
                                   uint32_t textureId, uint32_t maxParticles) {
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager || !textureManager) {
        ReleaseResources();
        return;
    }

    std::vector<ParticleEmitterSettings> pendingBeforeInitialize;
    pendingBeforeInitialize.swap(pendingEmitSettings_);
    ReleaseResources();

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
    if (!srvManager_->CanAllocate(kRequiredSrvDescriptors)) {
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
    if (ownsUploadPass) {
        dxCommon_->BeginUpload();
    }
    ParticleUploadPassScope uploadPass(dxCommon_, ownsUploadPass);
    if (!dxCommon_->IsCommandListRecording()) {
        return;
    }
    CreateParticleBuffer(particles);
    CreateFreeListBuffers();
    CreateActiveDrawBuffers();
    uploadPass.Finish();

    CreateConstantBuffers();

    if (!particleResource_ || !freeListResource_ ||
        !freeListIndexResource_ || !activeIndexResource_ ||
        !activeCountResource_ || !drawArgsResource_ ||
        !updateConstantBuffer_ || !drawConstantBuffer_ ||
        mappedUpdateCB_ == nullptr || mappedDrawCB_ == nullptr ||
        particleSrvGpuHandle_.ptr == 0 || particleUavGpuHandle_.ptr == 0 ||
        freeListUavGpuHandle_.ptr == 0 ||
        freeListIndexUavGpuHandle_.ptr == 0 ||
        activeIndexSrvGpuHandle_.ptr == 0 ||
        activeIndexUavGpuHandle_.ptr == 0 ||
        activeCountUavGpuHandle_.ptr == 0 ||
        drawArgsUavGpuHandle_.ptr == 0) {
        return;
    }

    if (!pendingEmitSettings_.empty() && mappedUpdateCB_) {
        mappedUpdateCB_->time = {totalTime_, 0.0f,
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
        drawPSO_ = GetOrCreateParticleDrawPso(
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
    pendingEmitSettings_.push_back(normalized);
    if (mappedUpdateCB_ && !updatePending_) {
        mappedUpdateCB_->time = {totalTime_, 0.0f,
                                 static_cast<float>(maxParticles_), 0.0f};
        updatePending_ = true;
    }
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

    if (!mappedUpdateCB_) {
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
            pendingEmitSettings_.push_back(emitterSettings_);
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

    mappedUpdateCB_->time = {totalTime_, deltaTime,
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
        !drawConstantBuffer_ || mappedDrawCB_ == nullptr ||
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
    ID3D12DescriptorHeap *heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);

    if (updatePending_ && dxCommon_->IsCommandListRecording()) {
        DispatchUpdate();
    }

    XMMATRIX viewProjection = camera.GetView() * camera.GetProj();
    XMStoreFloat4x4(&mappedDrawCB_->viewProjection,
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
    mappedDrawCB_->cameraRight = {right.x, right.y, right.z, 0.0f};
    mappedDrawCB_->cameraUp = {up.x, up.y, up.z, 0.0f};
    mappedDrawCB_->tintColor = {1.0f, 1.0f, 1.0f, 1.0f};
    mappedDrawCB_->atlasInfo = {
        static_cast<float>((std::max)(1u, emitterSettings_.atlasColumns)),
        static_cast<float>((std::max)(1u, emitterSettings_.atlasRows)),
        0.0f,
        0.0f};
    mappedDrawCB_->materialParams0 = materialSettings_.params0;
    mappedDrawCB_->materialParams1 = materialSettings_.params1;

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
        0, drawConstantBuffer_->GetGPUVirtualAddress());
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
        !updateRootSignature_ || !updatePSO_ || !updateConstantBuffer_ ||
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

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(3);
    barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
        particleResource_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    if (activeIndexState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            activeIndexResource_.Get(), activeIndexState_,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        activeIndexState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (drawArgsState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            drawArgsResource_.Get(), drawArgsState_,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        drawArgsState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    cmd->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

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
    if (cmd == nullptr || !updateRootSignature_ || !updatePSO_ ||
        !updateConstantBuffer_) {
        return;
    }
    cmd->SetComputeRootSignature(updateRootSignature_.Get());
    cmd->SetPipelineState(updatePSO_.Get());
    cmd->SetComputeRootConstantBufferView(
        0, updateConstantBuffer_->GetGPUVirtualAddress());
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

void GPUParticleSystem::CreateRootSignatures() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    {
        static_assert((sizeof(EmitterForGPU) / sizeof(uint32_t)) + 2u + 6u <=
                          64u,
                      "GPUParticle update root signature exceeds 64 DWORDs");
        CD3DX12_ROOT_PARAMETER params[8]{};
        params[0].InitAsConstantBufferView(0);
        params[1].InitAsConstants(
            static_cast<UINT>(sizeof(EmitterForGPU) / sizeof(uint32_t)), 1);

        CD3DX12_DESCRIPTOR_RANGE particleRange{};
        particleRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        params[2].InitAsDescriptorTable(1, &particleRange);

        CD3DX12_DESCRIPTOR_RANGE freeListRange{};
        freeListRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
        params[3].InitAsDescriptorTable(1, &freeListRange);

        CD3DX12_DESCRIPTOR_RANGE freeListIndexRange{};
        freeListIndexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);
        params[4].InitAsDescriptorTable(1, &freeListIndexRange);

        CD3DX12_DESCRIPTOR_RANGE activeIndexRange{};
        activeIndexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3);
        params[5].InitAsDescriptorTable(1, &activeIndexRange);

        CD3DX12_DESCRIPTOR_RANGE activeCountRange{};
        activeCountRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 4);
        params[6].InitAsDescriptorTable(1, &activeCountRange);

        CD3DX12_DESCRIPTOR_RANGE drawArgsRange{};
        drawArgsRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 5);
        params[7].InitAsDescriptorTable(1, &drawArgsRange);

        CD3DX12_ROOT_SIGNATURE_DESC desc;
        desc.Init(_countof(params), params, 0, nullptr);

        ComPtr<ID3DBlob> blob, error;
        if (FAILED(D3D12SerializeRootSignature(
                &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
            !blob) {
            return;
        }
        if (FAILED(dxCommon_->GetDevice()->CreateRootSignature(
                0, blob->GetBufferPointer(), blob->GetBufferSize(),
                IID_PPV_ARGS(&updateRootSignature_))) ||
            !updateRootSignature_) {
            return;
        }
    }

    drawRootSignature_ =
        GetSharedParticleDrawRootSignature(dxCommon_->GetDevice());
}

void GPUParticleSystem::CreatePipelineStates() {
    auto *device = dxCommon_->GetDevice();
    if (device == nullptr || !updateRootSignature_ || !drawRootSignature_) {
        return;
    }

    auto cs = ShaderCompiler::Compile(ShaderPaths::ParticleUpdateCS, "main",
                                      "cs_6_6");
    if (!cs) {
        return;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC computePso{};
    computePso.pRootSignature = updateRootSignature_.Get();
    computePso.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
    if (FAILED(device->CreateComputePipelineState(
            &computePso, IID_PPV_ARGS(&updatePSO_))) ||
        !updatePSO_) {
        return;
    }

    drawCommandSignature_ = GetSharedParticleDrawCommandSignature(device);

    const std::wstring pixelShaderPath =
        materialSettings_.pixelShaderPath.empty()
            ? std::wstring(ShaderPaths::ParticlePS)
            : materialSettings_.pixelShaderPath;
    drawPSO_ = GetOrCreateParticleDrawPso(device, drawRootSignature_.Get(),
                                          pixelShaderPath);
}

void GPUParticleSystem::CreateParticleBuffer(
    const std::vector<ParticleForGPU> &particles) {
    const UINT bufferSize =
        CheckedByteSize(sizeof(ParticleForGPU), particles.size(),
                        "GPUParticleSystem particle buffer size overflow");
    if (bufferSize == 0) {
        return;
    }
    auto *device = dxCommon_->GetDevice();
    auto *cmdList = dxCommon_->GetCommandList();
    if (device == nullptr || cmdList == nullptr) {
        return;
    }

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto particleDesc = CD3DX12_RESOURCE_DESC::Buffer(
        bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &particleDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            particleResource_.GetAddressOf())) {
        return;
    }
    particleResource_->SetName(L"GPUParticleSystem.Particles");

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    if (!CreateCommittedResourceChecked(
            device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            particleUploadResource_.GetAddressOf())) {
        return;
    }
    particleUploadResource_->SetName(L"GPUParticleSystem.ParticlesUpload");

    uint8_t *mapped = nullptr;
    if (!MapResourceChecked(particleUploadResource_.Get(),
                            reinterpret_cast<void **>(&mapped))) {
        return;
    }
    std::memcpy(mapped, particles.data(), bufferSize);
    particleUploadResource_->Unmap(0, nullptr);

    cmdList->CopyBufferRegion(particleResource_.Get(), 0,
                              particleUploadResource_.Get(), 0, bufferSize);
    auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
        particleResource_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &toSrv);

    if (!AllocateSrvHandles(srvManager_, particleSrvIndex_,
                            particleSrvCpuHandle_, particleSrvGpuHandle_)) {
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = maxParticles_;
    srvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device->CreateShaderResourceView(particleResource_.Get(), &srvDesc,
                                     particleSrvCpuHandle_);

    if (!AllocateSrvHandles(srvManager_, particleUavIndex_,
                            particleUavCpuHandle_, particleUavGpuHandle_)) {
        return;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = maxParticles_;
    uavDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(particleResource_.Get(), nullptr,
                                      &uavDesc, particleUavCpuHandle_);
}

void GPUParticleSystem::CreateFreeListBuffers() {
    auto *device = dxCommon_->GetDevice();
    auto *cmdList = dxCommon_->GetCommandList();
    if (device == nullptr || cmdList == nullptr) {
        return;
    }
    const UINT freeListBufferSize =
        CheckedByteSize(sizeof(uint32_t), maxParticles_,
                        "GPUParticleSystem free list buffer size overflow");
    if (freeListBufferSize == 0) {
        return;
    }

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto freeListDesc = CD3DX12_RESOURCE_DESC::Buffer(
        freeListBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &freeListDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            freeListResource_.GetAddressOf())) {
        return;
    }
    freeListResource_->SetName(L"GPUParticleSystem.FreeList");

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto freeListUploadDesc = CD3DX12_RESOURCE_DESC::Buffer(freeListBufferSize);
    if (!CreateCommittedResourceChecked(
            device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &freeListUploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            freeListUploadResource_.GetAddressOf())) {
        return;
    }
    freeListUploadResource_->SetName(L"GPUParticleSystem.FreeListUpload");

    std::vector<uint32_t> freeList(maxParticles_);
    for (uint32_t index = 0; index < maxParticles_; ++index) {
        freeList[index] = index;
    }

    uint8_t *mappedFreeList = nullptr;
    if (!MapResourceChecked(freeListUploadResource_.Get(),
                            reinterpret_cast<void **>(&mappedFreeList))) {
        return;
    }
    std::memcpy(mappedFreeList, freeList.data(), freeListBufferSize);
    freeListUploadResource_->Unmap(0, nullptr);

    cmdList->CopyBufferRegion(freeListResource_.Get(), 0,
                              freeListUploadResource_.Get(), 0,
                              freeListBufferSize);
    auto freeListToUav = CD3DX12_RESOURCE_BARRIER::Transition(
        freeListResource_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->ResourceBarrier(1, &freeListToUav);

    if (!AllocateSrvHandles(srvManager_, freeListUavIndex_,
                            freeListUavCpuHandle_, freeListUavGpuHandle_)) {
        return;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = maxParticles_;
    uavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(freeListResource_.Get(), nullptr,
                                      &uavDesc, freeListUavCpuHandle_);

    constexpr UINT freeListIndexBufferSize = sizeof(int32_t);
    auto freeListIndexDesc = CD3DX12_RESOURCE_DESC::Buffer(
        freeListIndexBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &freeListIndexDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            freeListIndexResource_.GetAddressOf())) {
        return;
    }
    freeListIndexResource_->SetName(L"GPUParticleSystem.FreeListIndex");

    auto freeListIndexUploadDesc =
        CD3DX12_RESOURCE_DESC::Buffer(freeListIndexBufferSize);
    if (!CreateCommittedResourceChecked(
            device, &uploadHeap, D3D12_HEAP_FLAG_NONE,
            &freeListIndexUploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, freeListIndexUploadResource_.GetAddressOf())) {
        return;
    }
    freeListIndexUploadResource_->SetName(
        L"GPUParticleSystem.FreeListIndexUpload");

    int32_t *mappedFreeListIndex = nullptr;
    if (!MapResourceChecked(
            freeListIndexUploadResource_.Get(),
            reinterpret_cast<void **>(&mappedFreeListIndex))) {
        return;
    }
    *mappedFreeListIndex = static_cast<int32_t>(maxParticles_);
    freeListIndexUploadResource_->Unmap(0, nullptr);

    cmdList->CopyBufferRegion(
        freeListIndexResource_.Get(), 0, freeListIndexUploadResource_.Get(), 0,
        freeListIndexBufferSize);
    auto freeListIndexToUav = CD3DX12_RESOURCE_BARRIER::Transition(
        freeListIndexResource_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->ResourceBarrier(1, &freeListIndexToUav);

    if (!AllocateSrvHandles(srvManager_, freeListIndexUavIndex_,
                            freeListIndexUavCpuHandle_,
                            freeListIndexUavGpuHandle_)) {
        return;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC indexUavDesc{};
    indexUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    indexUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    indexUavDesc.Buffer.FirstElement = 0;
    indexUavDesc.Buffer.NumElements = 1;
    indexUavDesc.Buffer.StructureByteStride = sizeof(int32_t);
    indexUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(freeListIndexResource_.Get(), nullptr,
                                      &indexUavDesc,
                                      freeListIndexUavCpuHandle_);
}

void GPUParticleSystem::CreateActiveDrawBuffers() {
    auto *device = dxCommon_->GetDevice();
    auto *cmdList = dxCommon_->GetCommandList();
    if (device == nullptr || cmdList == nullptr || srvManager_ == nullptr) {
        return;
    }
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);

    const UINT activeIndexBufferSize =
        CheckedByteSize(sizeof(uint32_t), maxParticles_,
                        "GPUParticleSystem active index buffer size overflow");
    if (activeIndexBufferSize == 0) {
        return;
    }
    auto activeIndexDesc = CD3DX12_RESOURCE_DESC::Buffer(
        activeIndexBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &activeIndexDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            activeIndexResource_.GetAddressOf())) {
        return;
    }
    activeIndexResource_->SetName(L"GPUParticleSystem.ActiveIndex");
    activeIndexState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    if (!AllocateSrvHandles(srvManager_, activeIndexSrvIndex_,
                            activeIndexSrvCpuHandle_,
                            activeIndexSrvGpuHandle_)) {
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC activeIndexSrvDesc{};
    activeIndexSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    activeIndexSrvDesc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    activeIndexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    activeIndexSrvDesc.Buffer.FirstElement = 0;
    activeIndexSrvDesc.Buffer.NumElements = maxParticles_;
    activeIndexSrvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    activeIndexSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device->CreateShaderResourceView(activeIndexResource_.Get(),
                                     &activeIndexSrvDesc,
                                     activeIndexSrvCpuHandle_);

    if (!AllocateSrvHandles(srvManager_, activeIndexUavIndex_,
                            activeIndexUavCpuHandle_,
                            activeIndexUavGpuHandle_)) {
        return;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC activeIndexUavDesc{};
    activeIndexUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    activeIndexUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    activeIndexUavDesc.Buffer.FirstElement = 0;
    activeIndexUavDesc.Buffer.NumElements = maxParticles_;
    activeIndexUavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    activeIndexUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(activeIndexResource_.Get(), nullptr,
                                      &activeIndexUavDesc,
                                      activeIndexUavCpuHandle_);

    constexpr UINT counterBufferSize = 16;
    auto activeCountDesc = CD3DX12_RESOURCE_DESC::Buffer(
        counterBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &activeCountDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            activeCountResource_.GetAddressOf())) {
        return;
    }
    activeCountResource_->SetName(L"GPUParticleSystem.ActiveCount");

    if (!AllocateSrvHandles(srvManager_, activeCountUavIndex_,
                            activeCountUavCpuHandle_,
                            activeCountUavGpuHandle_)) {
        return;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC rawUavDesc{};
    rawUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    rawUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    rawUavDesc.Buffer.FirstElement = 0;
    rawUavDesc.Buffer.NumElements = counterBufferSize / sizeof(uint32_t);
    rawUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device->CreateUnorderedAccessView(activeCountResource_.Get(), nullptr,
                                      &rawUavDesc, activeCountUavCpuHandle_);

    const UINT drawArgsBufferSize = sizeof(D3D12_DRAW_ARGUMENTS);
    auto drawArgsDesc = CD3DX12_RESOURCE_DESC::Buffer(
        drawArgsBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &drawArgsDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            drawArgsResource_.GetAddressOf())) {
        return;
    }
    drawArgsResource_->SetName(L"GPUParticleSystem.DrawArgs");
    drawArgsState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    if (!AllocateSrvHandles(srvManager_, drawArgsUavIndex_,
                            drawArgsUavCpuHandle_, drawArgsUavGpuHandle_)) {
        return;
    }

    rawUavDesc.Buffer.NumElements = drawArgsBufferSize / sizeof(uint32_t);
    device->CreateUnorderedAccessView(drawArgsResource_.Get(), nullptr,
                                      &rawUavDesc, drawArgsUavCpuHandle_);

    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    if (heap == nullptr) {
        return;
    }
    ID3D12DescriptorHeap *heaps[] = {heap};
    cmdList->SetDescriptorHeaps(1, heaps);
    const UINT safeDrawArgs[4] = {6u, 0u, 0u, 0u};
    cmdList->ClearUnorderedAccessViewUint(
        drawArgsUavGpuHandle_, drawArgsUavCpuHandle_, drawArgsResource_.Get(),
        safeDrawArgs, 0, nullptr);
    auto drawArgsClearBarrier =
        CD3DX12_RESOURCE_BARRIER::UAV(drawArgsResource_.Get());
    cmdList->ResourceBarrier(1, &drawArgsClearBarrier);
}

void GPUParticleSystem::CreateConstantBuffers() {
    auto *device = dxCommon_->GetDevice();
    if (device == nullptr) {
        return;
    }
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);

    const UINT updateSize = Align256(sizeof(UpdateConstantBufferData));
    if (updateSize == 0) {
        return;
    }
    auto updateDesc = CD3DX12_RESOURCE_DESC::Buffer(updateSize);
    if (!CreateCommittedResourceChecked(
            device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &updateDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            updateConstantBuffer_.GetAddressOf())) {
        return;
    }
    if (!MapResourceChecked(updateConstantBuffer_.Get(),
                            reinterpret_cast<void **>(&mappedUpdateCB_))) {
        return;
    }

    const UINT drawSize = Align256(sizeof(DrawConstantBufferData));
    if (drawSize == 0) {
        return;
    }
    auto drawDesc = CD3DX12_RESOURCE_DESC::Buffer(drawSize);
    if (!CreateCommittedResourceChecked(
            device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &drawDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            drawConstantBuffer_.GetAddressOf())) {
        return;
    }
    if (!MapResourceChecked(drawConstantBuffer_.Get(),
                            reinterpret_cast<void **>(&mappedDrawCB_))) {
        return;
    }
}

void GPUParticleSystem::ReleaseResources() {

    const bool hasGpuResources =
        updateConstantBuffer_ || drawConstantBuffer_ ||
        particleResource_ || particleUploadResource_ || freeListResource_ ||
        freeListUploadResource_ || freeListIndexResource_ ||
        freeListIndexUploadResource_ || activeIndexResource_ ||
        activeCountResource_ || drawArgsResource_;
    if (hasGpuResources && dxCommon_ && !dxCommon_->IsDeviceRemoved() &&
        !dxCommon_->IsCommandListRecording()) {
        dxCommon_->WaitForGpuIfPossible();
    }

    if (srvManager_) {
        if (particleSrvIndex_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(particleSrvIndex_);
            particleSrvIndex_ = UINT32_MAX;
        }
        if (particleUavIndex_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(particleUavIndex_);
            particleUavIndex_ = UINT32_MAX;
        }
        if (freeListUavIndex_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(freeListUavIndex_);
            freeListUavIndex_ = UINT32_MAX;
        }
        if (freeListIndexUavIndex_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(freeListIndexUavIndex_);
            freeListIndexUavIndex_ = UINT32_MAX;
        }
        if (activeIndexSrvIndex_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(activeIndexSrvIndex_);
            activeIndexSrvIndex_ = UINT32_MAX;
        }
        if (activeIndexUavIndex_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(activeIndexUavIndex_);
            activeIndexUavIndex_ = UINT32_MAX;
        }
        if (activeCountUavIndex_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(activeCountUavIndex_);
            activeCountUavIndex_ = UINT32_MAX;
        }
        if (drawArgsUavIndex_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(drawArgsUavIndex_);
            drawArgsUavIndex_ = UINT32_MAX;
        }
    }

    if (updateConstantBuffer_ && mappedUpdateCB_) {
        updateConstantBuffer_->Unmap(0, nullptr);
        mappedUpdateCB_ = nullptr;
    }
    if (drawConstantBuffer_ && mappedDrawCB_) {
        drawConstantBuffer_->Unmap(0, nullptr);
        mappedDrawCB_ = nullptr;
    }

    updateConstantBuffer_.Reset();
    drawConstantBuffer_.Reset();
    particleResource_.Reset();
    particleUploadResource_.Reset();
    freeListResource_.Reset();
    freeListUploadResource_.Reset();
    freeListIndexResource_.Reset();
    freeListIndexUploadResource_.Reset();
    activeIndexResource_.Reset();
    activeCountResource_.Reset();
    drawArgsResource_.Reset();
    updatePSO_.Reset();
    drawPSO_.Reset();
    updateRootSignature_.Reset();
    drawRootSignature_.Reset();
    drawCommandSignature_.Reset();
    activeIndexState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    drawArgsState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    updatePending_ = false;
    activeTimeRemaining_ = 0.0f;
    pendingEmitSettings_.clear();
    particleSrvIndex_ = UINT32_MAX;
    particleUavIndex_ = UINT32_MAX;
    freeListUavIndex_ = UINT32_MAX;
    freeListIndexUavIndex_ = UINT32_MAX;
    activeIndexSrvIndex_ = UINT32_MAX;
    activeIndexUavIndex_ = UINT32_MAX;
    activeCountUavIndex_ = UINT32_MAX;
    drawArgsUavIndex_ = UINT32_MAX;
    particleSrvGpuHandle_ = {};
    particleSrvCpuHandle_ = {};
    particleUavGpuHandle_ = {};
    particleUavCpuHandle_ = {};
    freeListUavGpuHandle_ = {};
    freeListUavCpuHandle_ = {};
    freeListIndexUavGpuHandle_ = {};
    freeListIndexUavCpuHandle_ = {};
    activeIndexSrvGpuHandle_ = {};
    activeIndexSrvCpuHandle_ = {};
    activeIndexUavGpuHandle_ = {};
    activeIndexUavCpuHandle_ = {};
    activeCountUavGpuHandle_ = {};
    activeCountUavCpuHandle_ = {};
    drawArgsUavGpuHandle_ = {};
    drawArgsUavCpuHandle_ = {};
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    textureManager_ = nullptr;
}

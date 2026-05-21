#include "particle/GPUParticleSystem.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <random>
#include <stdexcept>

using namespace DirectX;
using namespace DxUtils;
using Microsoft::WRL::ComPtr;

namespace {

constexpr uint32_t kParticleThreadCount = 256u;

ID3D12Device *gCachedParticleDrawDevice = nullptr;
ComPtr<ID3D12RootSignature> gCachedParticleDrawRootSignature;
std::map<std::wstring, ComPtr<ID3D12PipelineState>> gParticleDrawPsoCache;

ParticleEmitterSettings
NormalizeParticleEmitterSettings(ParticleEmitterSettings settings) {
    settings.maxParticles = (std::max)(1u, settings.maxParticles);
    settings.emitRate = (std::max)(0.0f, settings.emitRate);
    settings.burstCount = (std::max)(1u, settings.burstCount);
    settings.spawnOffsetScale.x = (std::max)(0.0f, settings.spawnOffsetScale.x);
    settings.spawnOffsetScale.y = (std::max)(0.0f, settings.spawnOffsetScale.y);
    settings.spawnOffsetScale.z = (std::max)(0.0f, settings.spawnOffsetScale.z);
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
    gParticleDrawPsoCache.clear();
}

ID3D12RootSignature *GetSharedParticleDrawRootSignature(ID3D12Device *device) {
    ResetParticleDrawCacheIfDeviceChanged(device);
    if (gCachedParticleDrawRootSignature) {
        return gCachedParticleDrawRootSignature.Get();
    }

    CD3DX12_ROOT_PARAMETER params[4];
    params[0].InitAsConstantBufferView(0);

    CD3DX12_DESCRIPTOR_RANGE particleRange;
    particleRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &particleRange);

    CD3DX12_DESCRIPTOR_RANGE textureRange;
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    params[2].InitAsDescriptorTable(1, &textureRange);

    CD3DX12_DESCRIPTOR_RANGE noiseTextureRange;
    noiseTextureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
    params[3].InitAsDescriptorTable(1, &noiseTextureRange);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> blob, error;
    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "D3D12SerializeRootSignature(GPUParticleDraw) failed");
    ThrowIfFailed(device->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&gCachedParticleDrawRootSignature)),
                  "CreateRootSignature(GPUParticleDraw) failed");
    return gCachedParticleDrawRootSignature.Get();
}

ID3D12PipelineState *GetOrCreateParticleDrawPso(
    ID3D12Device *device, ID3D12RootSignature *rootSignature,
    const std::wstring &pixelShaderPath) {
    ResetParticleDrawCacheIfDeviceChanged(device);

    auto found = gParticleDrawPsoCache.find(pixelShaderPath);
    if (found != gParticleDrawPsoCache.end()) {
        return found->second.Get();
    }

    auto vs =
        ShaderCompiler::Compile(ShaderPaths::ParticleVS, "main", "vs_5_0");
    auto ps = ShaderCompiler::Compile(pixelShaderPath, "main", "ps_5_0");

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
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    drawPso.DepthStencilState = depth;

    ComPtr<ID3D12PipelineState> pso;
    ThrowIfFailed(device->CreateGraphicsPipelineState(
                      &drawPso, IID_PPV_ARGS(&pso)),
                  "CreateGraphicsPipelineState(GPUParticleDraw) failed");

    ID3D12PipelineState *result = pso.Get();
    gParticleDrawPsoCache[pixelShaderPath] = std::move(pso);
    return result;
}

}

GPUParticleSystem::~GPUParticleSystem() { ReleaseResources(); }

void GPUParticleSystem::Initialize(DirectXCommon *dxCommon,
                                   SrvManager *srvManager,
                                   TextureManager *textureManager,
                                   uint32_t textureId, uint32_t maxParticles) {
    ReleaseResources();

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    textureManager_ = textureManager;
    textureId_ = textureId;
    maxParticles_ = (std::max)(1u, maxParticles);
    totalTime_ = 0.0f;
    emitterFrequencyTime_ = 0.0f;
    emitterSettings_ = NormalizeParticleEmitterSettings(ParticleEmitterSettings{});
    emitOncePending_ = false;

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
    }

    CreateRootSignatures();
    CreatePipelineStates();
    CreateParticleBuffer(particles);
    CreateFreeListBuffers();
    CreateConstantBuffers();
}

void GPUParticleSystem::SetEmitterSettings(
    const ParticleEmitterSettings &settings) {
    emitterSettings_ = NormalizeParticleEmitterSettings(settings);
}

void GPUParticleSystem::SetTextureFromFile(const std::wstring &filePath) {
    if (!textureManager_) {
        throw std::runtime_error(
            "GPUParticleSystem::SetTextureFromFile requires TextureManager");
    }

    textureId_ = textureManager_->Load(filePath);
}

void GPUParticleSystem::SetMaterialSettings(
    const GPUParticleMaterialSettings &settings) {
    materialSettings_ = settings;
}

void GPUParticleSystem::EmitOnce(const ParticleEmitterSettings &settings) {
    SetEmitterSettings(settings);
    emitterFrequencyTime_ = 0.0f;
    emitOncePending_ = true;
}

void GPUParticleSystem::Update(float deltaTime) {
    totalTime_ += deltaTime;

    if (!mappedUpdateCB_ || !mappedEmitterCB_) {
        return;
    }

    emitterFrequencyTime_ += deltaTime;
    uint32_t emit = 0;
    if (emitOncePending_) {
        emitOncePending_ = false;
        emit = 1;
    } else if (emitterSettings_.emissionType ==
                   ParticleEmissionType::Continuous &&
               emitterSettings_.emitRate > 0.0f &&
               emitterFrequencyTime_ >= 1.0f / emitterSettings_.emitRate) {
        emitterFrequencyTime_ -= 1.0f / emitterSettings_.emitRate;
        emit = 1;
    }

    mappedUpdateCB_->time = {totalTime_, deltaTime,
                             static_cast<float>(maxParticles_), 0.0f};
    *mappedEmitterCB_ = BuildEmitterForGPU(emit);

    updatePending_ = true;
    if (dxCommon_ && dxCommon_->IsCommandListRecording()) {
        DispatchUpdate();
    }
}

void GPUParticleSystem::Draw(const Camera &camera) {
    if (!dxCommon_ || !srvManager_ || !textureManager_ || !particleResource_) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    if (updatePending_ && dxCommon_->IsCommandListRecording()) {
        DispatchUpdate();
    }

    XMMATRIX viewProjection = camera.GetView() * camera.GetProj();
    XMStoreFloat4x4(&mappedDrawCB_->viewProjection,
                    XMMatrixTranspose(viewProjection));

    XMMATRIX billboard = camera.GetView();
    billboard.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    billboard = XMMatrixInverse(nullptr, billboard);

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

    const uint32_t noiseTextureId =
        materialSettings_.noiseTextureId != UINT32_MAX
            ? materialSettings_.noiseTextureId
            : textureManager_->GetWhiteTextureId();

    cmd->SetGraphicsRootSignature(drawRootSignature_.Get());
    cmd->SetPipelineState(drawPSO_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootConstantBufferView(
        0, drawConstantBuffer_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, particleSrvGpuHandle_);
    cmd->SetGraphicsRootDescriptorTable(
        2, textureManager_->GetGpuHandle(textureId_));
    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(noiseTextureId));
    cmd->DrawInstanced(6, maxParticles_, 0, 0);
}

void GPUParticleSystem::DispatchUpdate() {
    if (!dxCommon_ || !srvManager_ || !particleResource_ ||
        !updateConstantBuffer_ || !emitterConstantBuffer_) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(
        particleResource_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmd->ResourceBarrier(1, &toUav);

    cmd->SetComputeRootSignature(updateRootSignature_.Get());
    cmd->SetPipelineState(updatePSO_.Get());
    cmd->SetComputeRootConstantBufferView(
        0, updateConstantBuffer_->GetGPUVirtualAddress());
    cmd->SetComputeRootConstantBufferView(
        1, emitterConstantBuffer_->GetGPUVirtualAddress());
    cmd->SetComputeRootDescriptorTable(2, particleUavGpuHandle_);
    cmd->SetComputeRootDescriptorTable(3, freeListUavGpuHandle_);
    cmd->SetComputeRootDescriptorTable(4, freeListIndexUavGpuHandle_);
    cmd->Dispatch((maxParticles_ + kParticleThreadCount - 1u) /
                      kParticleThreadCount,
                  1, 1);

    auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(particleResource_.Get());
    cmd->ResourceBarrier(1, &uavBarrier);

    auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
        particleResource_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmd->ResourceBarrier(1, &toSrv);
    updatePending_ = false;
}

GPUParticleSystem::EmitterForGPU
GPUParticleSystem::BuildEmitterForGPU(uint32_t emit) const {
    EmitterForGPU emitter{};
    emitter.position = {emitterSettings_.position.x, emitterSettings_.position.y,
                        emitterSettings_.position.z, 0.0f};
    emitter.spawnOffsetScale = {
        emitterSettings_.spawnOffsetScale.x, emitterSettings_.spawnOffsetScale.y,
        emitterSettings_.spawnOffsetScale.z, 0.0f};
    emitter.directionAndDirectionalVelocity = {
        emitterSettings_.direction.x, emitterSettings_.direction.y,
        emitterSettings_.direction.z, emitterSettings_.directionalVelocity};
    emitter.velocityBiasAndRadialVelocity = {
        emitterSettings_.velocityBias.x, emitterSettings_.velocityBias.y,
        emitterSettings_.velocityBias.z, emitterSettings_.radialVelocity};
    emitter.lifeAndFade = {emitterSettings_.baseLifeTime,
                           emitterSettings_.lifeTimeRandom,
                           emitterSettings_.fadeInTime,
                           emitterSettings_.fadeOutTime};
    emitter.scale = {emitterSettings_.startScale, emitterSettings_.endScale,
                     emitterSettings_.scaleRandom, emitterSettings_.stretch};
    emitter.accelerationAndTurbulence = {
        emitterSettings_.acceleration.x, emitterSettings_.acceleration.y,
        emitterSettings_.acceleration.z, emitterSettings_.turbulence};
    emitter.motion = {emitterSettings_.damping, emitterSettings_.fadeOutPower,
                      emitterSettings_.emitRate, 0.0f};
    emitter.atlasAndRotation = {
        static_cast<float>(emitterSettings_.atlasFrameStart),
        static_cast<float>(emitterSettings_.atlasFrameCount),
        emitterSettings_.rotationSpeed, 0.0f};
    emitter.tintColor = emitterSettings_.tintColor;
    emitter.config = {
        static_cast<uint32_t>(emitterSettings_.emissionType),
        static_cast<uint32_t>(emitterSettings_.spawnShape),
        (std::min)(emitterSettings_.burstCount, maxParticles_), emit};
    return emitter;
}

void GPUParticleSystem::CreateRootSignatures() {
    {
        CD3DX12_ROOT_PARAMETER params[5];
        params[0].InitAsConstantBufferView(0);
        params[1].InitAsConstantBufferView(1);

        CD3DX12_DESCRIPTOR_RANGE particleRange;
        particleRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        params[2].InitAsDescriptorTable(1, &particleRange);

        CD3DX12_DESCRIPTOR_RANGE freeListRange;
        freeListRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
        params[3].InitAsDescriptorTable(1, &freeListRange);

        CD3DX12_DESCRIPTOR_RANGE freeListIndexRange;
        freeListIndexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);
        params[4].InitAsDescriptorTable(1, &freeListIndexRange);

        CD3DX12_ROOT_SIGNATURE_DESC desc;
        desc.Init(_countof(params), params, 0, nullptr);

        ComPtr<ID3DBlob> blob, error;
        ThrowIfFailed(D3D12SerializeRootSignature(
                          &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                      "D3D12SerializeRootSignature(GPUParticleUpdate) failed");
        ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                          0, blob->GetBufferPointer(), blob->GetBufferSize(),
                          IID_PPV_ARGS(&updateRootSignature_)),
                      "CreateRootSignature(GPUParticleUpdate) failed");
    }

    drawRootSignature_ =
        GetSharedParticleDrawRootSignature(dxCommon_->GetDevice());
}

void GPUParticleSystem::CreatePipelineStates() {
    auto *device = dxCommon_->GetDevice();

    auto cs = ShaderCompiler::Compile(ShaderPaths::ParticleUpdateCS, "main",
                                      "cs_5_0");
    D3D12_COMPUTE_PIPELINE_STATE_DESC computePso{};
    computePso.pRootSignature = updateRootSignature_.Get();
    computePso.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
    ThrowIfFailed(device->CreateComputePipelineState(&computePso,
                                                     IID_PPV_ARGS(&updatePSO_)),
                  "CreateComputePipelineState(GPUParticleUpdate) failed");

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
        static_cast<UINT>(sizeof(ParticleForGPU) * particles.size());
    auto *device = dxCommon_->GetDevice();

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto particleDesc = CD3DX12_RESOURCE_DESC::Buffer(
        bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(device->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &particleDesc,
                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                      IID_PPV_ARGS(&particleResource_)),
                  "CreateCommittedResource(GPUParticleBuffer) failed");

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    ThrowIfFailed(device->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&particleUploadResource_)),
                  "CreateCommittedResource(GPUParticleUpload) failed");

    uint8_t *mapped = nullptr;
    ThrowIfFailed(particleUploadResource_->Map(
                      0, nullptr, reinterpret_cast<void **>(&mapped)),
                  "GPUParticleUpload Map failed");
    std::memcpy(mapped, particles.data(), bufferSize);
    particleUploadResource_->Unmap(0, nullptr);

    dxCommon_->GetCommandList()->CopyBufferRegion(particleResource_.Get(), 0,
                                                  particleUploadResource_.Get(),
                                                  0, bufferSize);
    auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
        particleResource_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    dxCommon_->GetCommandList()->ResourceBarrier(1, &toSrv);

    particleSrvIndex_ = srvManager_->Allocate();
    particleSrvCpuHandle_ = srvManager_->GetCpuHandle(particleSrvIndex_);
    particleSrvGpuHandle_ = srvManager_->GetGpuHandle(particleSrvIndex_);

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

    particleUavIndex_ = srvManager_->Allocate();
    particleUavCpuHandle_ = srvManager_->GetCpuHandle(particleUavIndex_);
    particleUavGpuHandle_ = srvManager_->GetGpuHandle(particleUavIndex_);

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
    const UINT freeListBufferSize = sizeof(uint32_t) * maxParticles_;

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto freeListDesc = CD3DX12_RESOURCE_DESC::Buffer(
        freeListBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(device->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &freeListDesc,
                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                      IID_PPV_ARGS(&freeListResource_)),
                  "CreateCommittedResource(GPUParticleFreeList) failed");

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto freeListUploadDesc = CD3DX12_RESOURCE_DESC::Buffer(freeListBufferSize);
    ThrowIfFailed(device->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &freeListUploadDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&freeListUploadResource_)),
                  "CreateCommittedResource(GPUParticleFreeListUpload) failed");

    std::vector<uint32_t> freeList(maxParticles_);
    for (uint32_t index = 0; index < maxParticles_; ++index) {
        freeList[index] = index;
    }

    uint8_t *mappedFreeList = nullptr;
    ThrowIfFailed(freeListUploadResource_->Map(
                      0, nullptr, reinterpret_cast<void **>(&mappedFreeList)),
                  "GPUParticleFreeListUpload Map failed");
    std::memcpy(mappedFreeList, freeList.data(), freeListBufferSize);
    freeListUploadResource_->Unmap(0, nullptr);

    dxCommon_->GetCommandList()->CopyBufferRegion(freeListResource_.Get(), 0,
                                                  freeListUploadResource_.Get(),
                                                  0, freeListBufferSize);
    auto freeListToUav = CD3DX12_RESOURCE_BARRIER::Transition(
        freeListResource_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    dxCommon_->GetCommandList()->ResourceBarrier(1, &freeListToUav);

    freeListUavIndex_ = srvManager_->Allocate();
    freeListUavCpuHandle_ = srvManager_->GetCpuHandle(freeListUavIndex_);
    freeListUavGpuHandle_ = srvManager_->GetGpuHandle(freeListUavIndex_);

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
    ThrowIfFailed(device->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &freeListIndexDesc,
                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                      IID_PPV_ARGS(&freeListIndexResource_)),
                  "CreateCommittedResource(GPUParticleFreeListIndex) failed");

    auto freeListIndexUploadDesc =
        CD3DX12_RESOURCE_DESC::Buffer(freeListIndexBufferSize);
    ThrowIfFailed(
        device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &freeListIndexUploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&freeListIndexUploadResource_)),
        "CreateCommittedResource(GPUParticleFreeListIndexUpload) failed");

    int32_t *mappedFreeListIndex = nullptr;
    ThrowIfFailed(
        freeListIndexUploadResource_->Map(
            0, nullptr, reinterpret_cast<void **>(&mappedFreeListIndex)),
        "GPUParticleFreeListIndexUpload Map failed");
    *mappedFreeListIndex = static_cast<int32_t>(maxParticles_);
    freeListIndexUploadResource_->Unmap(0, nullptr);

    dxCommon_->GetCommandList()->CopyBufferRegion(
        freeListIndexResource_.Get(), 0, freeListIndexUploadResource_.Get(), 0,
        freeListIndexBufferSize);
    auto freeListIndexToUav = CD3DX12_RESOURCE_BARRIER::Transition(
        freeListIndexResource_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    dxCommon_->GetCommandList()->ResourceBarrier(1, &freeListIndexToUav);

    freeListIndexUavIndex_ = srvManager_->Allocate();
    freeListIndexUavCpuHandle_ =
        srvManager_->GetCpuHandle(freeListIndexUavIndex_);
    freeListIndexUavGpuHandle_ =
        srvManager_->GetGpuHandle(freeListIndexUavIndex_);

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

void GPUParticleSystem::CreateConstantBuffers() {
    auto *device = dxCommon_->GetDevice();
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);

    auto updateDesc = CD3DX12_RESOURCE_DESC::Buffer(
        Align256(sizeof(UpdateConstantBufferData)));
    ThrowIfFailed(device->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &updateDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&updateConstantBuffer_)),
                  "CreateCommittedResource(GPUParticleUpdateCB) failed");
    ThrowIfFailed(updateConstantBuffer_->Map(
                      0, nullptr, reinterpret_cast<void **>(&mappedUpdateCB_)),
                  "GPUParticleUpdateCB Map failed");

    auto emitterDesc =
        CD3DX12_RESOURCE_DESC::Buffer(Align256(sizeof(EmitterForGPU)));
    ThrowIfFailed(device->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &emitterDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&emitterConstantBuffer_)),
                  "CreateCommittedResource(GPUParticleEmitterCB) failed");
    ThrowIfFailed(emitterConstantBuffer_->Map(
                      0, nullptr, reinterpret_cast<void **>(&mappedEmitterCB_)),
                  "GPUParticleEmitterCB Map failed");

    auto drawDesc =
        CD3DX12_RESOURCE_DESC::Buffer(Align256(sizeof(DrawConstantBufferData)));
    ThrowIfFailed(device->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &drawDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&drawConstantBuffer_)),
                  "CreateCommittedResource(GPUParticleDrawCB) failed");
    ThrowIfFailed(drawConstantBuffer_->Map(
                      0, nullptr, reinterpret_cast<void **>(&mappedDrawCB_)),
                  "GPUParticleDrawCB Map failed");
}

void GPUParticleSystem::ReleaseResources() {
    if (srvManager_) {
        if (particleSrvIndex_ != UINT32_MAX) {
            srvManager_->Free(particleSrvIndex_);
            particleSrvIndex_ = UINT32_MAX;
        }
        if (particleUavIndex_ != UINT32_MAX) {
            srvManager_->Free(particleUavIndex_);
            particleUavIndex_ = UINT32_MAX;
        }
        if (freeListUavIndex_ != UINT32_MAX) {
            srvManager_->Free(freeListUavIndex_);
            freeListUavIndex_ = UINT32_MAX;
        }
        if (freeListIndexUavIndex_ != UINT32_MAX) {
            srvManager_->Free(freeListIndexUavIndex_);
            freeListIndexUavIndex_ = UINT32_MAX;
        }
    }

    if (updateConstantBuffer_ && mappedUpdateCB_) {
        updateConstantBuffer_->Unmap(0, nullptr);
        mappedUpdateCB_ = nullptr;
    }
    if (emitterConstantBuffer_ && mappedEmitterCB_) {
        emitterConstantBuffer_->Unmap(0, nullptr);
        mappedEmitterCB_ = nullptr;
    }
    if (drawConstantBuffer_ && mappedDrawCB_) {
        drawConstantBuffer_->Unmap(0, nullptr);
        mappedDrawCB_ = nullptr;
    }

    updateConstantBuffer_.Reset();
    emitterConstantBuffer_.Reset();
    drawConstantBuffer_.Reset();
    particleResource_.Reset();
    particleUploadResource_.Reset();
    freeListResource_.Reset();
    freeListUploadResource_.Reset();
    freeListIndexResource_.Reset();
    freeListIndexUploadResource_.Reset();
    updatePSO_.Reset();
    drawPSO_.Reset();
    updateRootSignature_.Reset();
    drawRootSignature_.Reset();
    updatePending_ = false;
}


#include "graphics/PostProcessSystem.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

float AtLeastFinite(float value, float fallback, float minimum) {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return (std::max)(value, minimum);
}

float ClampFinite(float value, float fallback, float minimum, float maximum) {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

template <typename Enum>
int32_t ValidModeOrNone(Enum mode, int32_t minValue, int32_t maxValue) {
    const int32_t value = static_cast<int32_t>(mode);
    return value >= minValue && value <= maxValue ? value : 0;
}

template <size_t N>
void CopyFinite(float (&dst)[N], const float (&src)[N],
                const float (&fallback)[N]) {
    for (size_t i = 0; i < N; ++i) {
        dst[i] = FiniteOr(src[i], fallback[i]);
    }
}

bool HasSpecial(const PostProcessProfile &profile) {
    return profile.special.mode == PostProcessSpecialMode::Vignette ||
           profile.special.mode == PostProcessSpecialMode::Dissolve;
}

bool HasVignette(const PostProcessProfile &profile) {
    return (profile.vignette.enabled && profile.vignette.strength > 0.0f) ||
           profile.vignette.primaryTintStrength > 0.0f ||
           profile.vignette.secondaryTintStrength > 0.0f;
}

bool HasRandomNoise(const PostProcessProfile &profile) {
    return profile.randomNoise.mode != PostProcessRandomMode::None &&
           profile.randomNoise.strength > 0.0f;
}

bool HasToon(const PostProcessProfile &profile) {
    return profile.toon.enabled && profile.toon.strength > 0.0f;
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
    D3D12_RESOURCE_STATES initialState, ID3D12Resource **resource) {
    if (device == nullptr || heapProperties == nullptr ||
        resourceDesc == nullptr || resource == nullptr) {
        return false;
    }
    *resource = nullptr;
    return SUCCEEDED(device->CreateCommittedResource(
        heapProperties, heapFlags, resourceDesc, initialState, nullptr,
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
} // namespace

PostProcessSystem::~PostProcessSystem() {
    Finalize();
}

void PostProcessSystem::Initialize(DirectXCommon *dxCommon,
                                    SrvManager *srvManager, int width,
                                    int height) {
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager) {
        Finalize();
        return;
    }

    Finalize();

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;

    CreateRootSignature();
    CreatePipelineState();
    CreateConstantBuffer();
    Resize(width, height);
    if (!rootSignature_ || !pipelineState_ || !copyPipelineState_ ||
        !constBuffer_ || mappedConstBuffer_ == nullptr) {
        Finalize();
    }
}

void PostProcessSystem::Finalize() {
    if (constBuffer_ && dxCommon_ != nullptr && !dxCommon_->IsDeviceRemoved() &&
        !dxCommon_->IsCommandListRecording()) {
        dxCommon_->WaitForGpuIfPossible();
    }

    if (constBuffer_ && mappedConstBuffer_ != nullptr) {
        constBuffer_->Unmap(0, nullptr);
        mappedConstBuffer_ = nullptr;
    }

    constBuffer_.Reset();
    pipelineState_.Reset();
    copyPipelineState_.Reset();
    rootSignature_.Reset();
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
}

void PostProcessSystem::Resize(int width, int height) {
    if (!dxCommon_ || !srvManager_) {
        return;
    }

    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;

    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.Width = static_cast<float>(width_);
    viewport_.Height = static_cast<float>(height_);
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;

    scissorRect_.left = 0;
    scissorRect_.top = 0;
    scissorRect_.right = width_;
    scissorRect_.bottom = height_;

    UpdateConstantBuffer();
}

void PostProcessSystem::SetProfile(const PostProcessProfile &profile) {
    profile_ = profile;
    UpdateConstantBuffer();
}

bool PostProcessSystem::RequiresPostProcess() const {
    return profile_.colorGrade.mode != PostProcessColorMode::None ||
           profile_.filter.mode != PostProcessFilterMode::None ||
           profile_.edge.mode != PostProcessEdgeMode::None ||
           profile_.tonemap.enabled || profile_.bloom.enabled ||
           profile_.noise.enabled || HasSpecial(profile_) ||
           profile_.lensFlare.enabled || HasVignette(profile_) ||
           HasRandomNoise(profile_) ||
           profile_.radialBlur.strength > 0.0f ||
           profile_.sceneDim.strength > 0.0f ||
           HasToon(profile_);
}

void PostProcessSystem::Draw(D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
                              D3D12_GPU_DESCRIPTOR_HANDLE depthHandle) {
    if (!dxCommon_ || !srvManager_ || !rootSignature_ || !pipelineState_ ||
        !copyPipelineState_ || !constBuffer_) {
        return;
    }
    if (textureHandle.ptr == 0 || depthHandle.ptr == 0) {
        return;
    }

    auto commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    if (commandList == nullptr || heap == nullptr ||
        constBuffer_->GetGPUVirtualAddress() == 0) {
        return;
    }

    ID3D12DescriptorHeap *heaps[] = {heap};
    commandList->SetDescriptorHeaps(1, heaps);

    commandList->RSSetViewports(1, &viewport_);
    commandList->RSSetScissorRects(1, &scissorRect_);
    commandList->SetPipelineState(
        RequiresPostProcess() ? pipelineState_.Get() : copyPipelineState_.Get());
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetGraphicsRootDescriptorTable(0, textureHandle);
    commandList->SetGraphicsRootDescriptorTable(1, depthHandle);
    commandList->SetGraphicsRootConstantBufferView(
        2, constBuffer_->GetGPUVirtualAddress());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
}

void PostProcessSystem::CreateRootSignature() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    CD3DX12_DESCRIPTOR_RANGE textureRange{};
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE depthRange{};
    depthRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

    CD3DX12_ROOT_PARAMETER params[3]{};
    params[0].InitAsDescriptorTable(1, &textureRange,
                                    D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsDescriptorTable(1, &depthRange,
                                    D3D12_SHADER_VISIBILITY_PIXEL);
    params[2].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC sampler{};
    sampler.Init(0);
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    if (FAILED(D3D12SerializeRootSignature(
            &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        !blob) {
        return;
    }

    if (FAILED(dxCommon_->GetDevice()->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature_)))) {
        rootSignature_.Reset();
    }
}

void PostProcessSystem::CreatePipelineState() {
    if (!dxCommon_ || !dxCommon_->GetDevice() || !rootSignature_) {
        return;
    }
    auto vs =
        ShaderCompiler::Compile(ShaderPaths::PostProcessVS, "main", "vs_6_6");
    auto ps =
        ShaderCompiler::Compile(ShaderPaths::PostProcessPS, "main", "ps_6_6");
    auto copyPs = ShaderCompiler::Compile(ShaderPaths::PostProcessCopyPS,
                                          "main", "ps_6_6");
    if (!vs || !ps || !copyPs) {
        return;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DirectXCommon::kBackBufferFormat;
    desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState = depth;
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&pipelineState_)))) {
        pipelineState_.Reset();
        return;
    }

    desc.PS = {copyPs->GetBufferPointer(), copyPs->GetBufferSize()};
    if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&copyPipelineState_)))) {
        copyPipelineState_.Reset();
    }
}

void PostProcessSystem::CreateConstantBuffer() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    const UINT size = Align256(sizeof(PostProcessConstants));
    if (size == 0) {
        return;
    }

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);

    if (!CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, constBuffer_.GetAddressOf())) {
        return;
    }

    if (!MapResourceChecked(constBuffer_.Get(),
                            reinterpret_cast<void **>(&mappedConstBuffer_))) {
        return;
    }

    UpdateConstantBuffer();
}

void PostProcessSystem::UpdateConstantBuffer() {
    if (!mappedConstBuffer_) {
        return;
    }

    const auto &color = profile_.colorGrade;
    const auto &filter = profile_.filter;
    const auto &edge = profile_.edge;
    const auto &tonemap = profile_.tonemap;
    const auto &bloom = profile_.bloom;
    const auto &noise = profile_.noise;
    const auto &special = profile_.special;
    const auto &vignette = profile_.vignette;
    const auto &radialBlur = profile_.radialBlur;
    const auto &randomNoise = profile_.randomNoise;
    const auto &sceneDim = profile_.sceneDim;
    const auto &toon = profile_.toon;
    const auto &dissolve = profile_.dissolve;
    const auto &lensFlare = profile_.lensFlare;
    const PostProcessProfile defaults{};

    float nearZ = AtLeastFinite(edge.nearZ, defaults.edge.nearZ, 0.0001f);
    float farZ = AtLeastFinite(edge.farZ, defaults.edge.farZ, 0.0002f);
    if (farZ <= nearZ) {
        farZ = (std::max)(defaults.edge.farZ, nearZ + 0.0001f);
    }

    mappedConstBuffer_->colorMode =
        ValidModeOrNone(color.mode, 0, static_cast<int32_t>(PostProcessColorMode::Sepia));
    mappedConstBuffer_->filterMode =
        ValidModeOrNone(filter.mode, 0, static_cast<int32_t>(PostProcessFilterMode::GaussianBlur7x7));
    mappedConstBuffer_->texelSize[0] = 1.0f / static_cast<float>(width_);
    mappedConstBuffer_->texelSize[1] = 1.0f / static_cast<float>(height_);
    mappedConstBuffer_->edgeMode =
        ValidModeOrNone(edge.mode, 0, static_cast<int32_t>(PostProcessEdgeMode::Depth));
    mappedConstBuffer_->luminanceEdgeThreshold =
        AtLeastFinite(edge.luminanceThreshold,
                      defaults.edge.luminanceThreshold, 0.0f);
    mappedConstBuffer_->depthEdgeThreshold =
        AtLeastFinite(edge.depthThreshold, defaults.edge.depthThreshold, 0.0f);
    mappedConstBuffer_->nearZ = nearZ;
    mappedConstBuffer_->farZ = farZ;
    CopyFinite(mappedConstBuffer_->grayscaleWeights, color.grayscaleWeights,
               defaults.colorGrade.grayscaleWeights);
    mappedConstBuffer_->tonemapEnabled = tonemap.enabled ? 1 : 0;
    mappedConstBuffer_->exposure =
        AtLeastFinite(tonemap.exposure, defaults.tonemap.exposure, 0.0f);
    mappedConstBuffer_->gamma =
        AtLeastFinite(tonemap.gamma, defaults.tonemap.gamma, 0.0001f);
    mappedConstBuffer_->bloomEnabled = bloom.enabled ? 1 : 0;
    mappedConstBuffer_->bloomThreshold =
        AtLeastFinite(bloom.threshold, defaults.bloom.threshold, 0.0f);
    mappedConstBuffer_->bloomIntensity =
        AtLeastFinite(bloom.intensity, defaults.bloom.intensity, 0.0f);
    mappedConstBuffer_->bloomRadius =
        AtLeastFinite(bloom.radius, defaults.bloom.radius, 0.0f);
    mappedConstBuffer_->noiseEnabled = noise.enabled ? 1 : 0;
    mappedConstBuffer_->noiseStrength =
        AtLeastFinite(noise.strength, defaults.noise.strength, 0.0f);
    mappedConstBuffer_->noiseScale = FiniteOr(noise.scale, defaults.noise.scale);
    mappedConstBuffer_->noiseTime = FiniteOr(noise.time, defaults.noise.time);
    mappedConstBuffer_->specialMode =
        ValidModeOrNone(special.mode, 0, static_cast<int32_t>(PostProcessSpecialMode::Dissolve));
    mappedConstBuffer_->vignetteStrength =
        AtLeastFinite(vignette.strength, defaults.vignette.strength, 0.0f);
    mappedConstBuffer_->vignetteRadius =
        FiniteOr(vignette.radius, defaults.vignette.radius);
    mappedConstBuffer_->radialBlurStrength =
        AtLeastFinite(radialBlur.strength, defaults.radialBlur.strength, 0.0f);
    mappedConstBuffer_->dissolveAmount =
        ClampFinite(dissolve.amount, defaults.dissolve.amount, 0.0f, 1.0f);
    mappedConstBuffer_->dissolveSoftness =
        AtLeastFinite(dissolve.softness, defaults.dissolve.softness, 0.0001f);
    mappedConstBuffer_->dissolveScale =
        FiniteOr(dissolve.scale, defaults.dissolve.scale);
    mappedConstBuffer_->lensFlareEnabled = lensFlare.enabled ? 1 : 0;
    mappedConstBuffer_->lensFlareVisibility =
        ClampFinite(lensFlare.visibility, defaults.lensFlare.visibility,
                    0.0f, 1.0f);
    mappedConstBuffer_->lensFlareSunUv[0] =
        FiniteOr(lensFlare.sunUv[0], defaults.lensFlare.sunUv[0]);
    mappedConstBuffer_->lensFlareSunUv[1] =
        FiniteOr(lensFlare.sunUv[1], defaults.lensFlare.sunUv[1]);
    mappedConstBuffer_->lensFlareSunDepth =
        FiniteOr(lensFlare.sunDepth, defaults.lensFlare.sunDepth);
    mappedConstBuffer_->lensFlareOcclusionBias =
        FiniteOr(lensFlare.occlusionBias, defaults.lensFlare.occlusionBias);
    mappedConstBuffer_->lensFlareGlareRadius =
        AtLeastFinite(lensFlare.glareRadius,
                      defaults.lensFlare.glareRadius, 0.0001f);
    mappedConstBuffer_->lensFlareGlareIntensity =
        AtLeastFinite(lensFlare.glareIntensity,
                      defaults.lensFlare.glareIntensity, 0.0f);
    mappedConstBuffer_->lensFlareGhostIntensity =
        AtLeastFinite(lensFlare.ghostIntensity,
                      defaults.lensFlare.ghostIntensity, 0.0f);
    mappedConstBuffer_->lensFlareStreakIntensity =
        AtLeastFinite(lensFlare.streakIntensity,
                      defaults.lensFlare.streakIntensity, 0.0f);
    mappedConstBuffer_->lensFlareStreakWidth =
        AtLeastFinite(lensFlare.streakWidth,
                      defaults.lensFlare.streakWidth, 0.0001f);
    mappedConstBuffer_->lensFlarePadding0 = 0.0f;
    mappedConstBuffer_->lensFlarePadding0b = 0.0f;
    CopyFinite(mappedConstBuffer_->lensFlareGlareColor,
               lensFlare.glareColor, defaults.lensFlare.glareColor);
    CopyFinite(mappedConstBuffer_->lensFlareGhostWarmColor,
               lensFlare.ghostWarmColor, defaults.lensFlare.ghostWarmColor);
    CopyFinite(mappedConstBuffer_->lensFlareGhostCoolColor,
               lensFlare.ghostCoolColor, defaults.lensFlare.ghostCoolColor);
    CopyFinite(mappedConstBuffer_->lensFlareStreakColor,
               lensFlare.streakColor, defaults.lensFlare.streakColor);
    mappedConstBuffer_->lensFlareGlareAlpha =
        ClampFinite(lensFlare.glareAlpha, defaults.lensFlare.glareAlpha,
                    0.0f, 1.0f);
    mappedConstBuffer_->lensFlareGhostAlpha =
        ClampFinite(lensFlare.ghostAlpha, defaults.lensFlare.ghostAlpha,
                    0.0f, 1.0f);
    mappedConstBuffer_->lensFlareStreakAlpha =
        ClampFinite(lensFlare.streakAlpha, defaults.lensFlare.streakAlpha,
                    0.0f, 1.0f);
    mappedConstBuffer_->lensFlarePadding1 = 0.0f;
    mappedConstBuffer_->enableVignetting = vignette.enabled ? 1 : 0;
    mappedConstBuffer_->randomMode =
        ValidModeOrNone(randomNoise.mode, 0, static_cast<int32_t>(PostProcessRandomMode::OverlayNoise));
    mappedConstBuffer_->radialBlurSampleCount =
        std::clamp(radialBlur.sampleCount, 0, 32);
    mappedConstBuffer_->vignettingScale =
        AtLeastFinite(vignette.scale, defaults.vignette.scale, 0.0f);
    mappedConstBuffer_->vignettingPower =
        AtLeastFinite(vignette.power, defaults.vignette.power, 0.0001f);
    mappedConstBuffer_->radialBlurCenter[0] =
        FiniteOr(radialBlur.center[0], defaults.radialBlur.center[0]);
    mappedConstBuffer_->radialBlurCenter[1] =
        FiniteOr(radialBlur.center[1], defaults.radialBlur.center[1]);
    mappedConstBuffer_->randomStrength =
        AtLeastFinite(randomNoise.strength, defaults.randomNoise.strength,
                      0.0f);
    mappedConstBuffer_->randomScale =
        FiniteOr(randomNoise.scale, defaults.randomNoise.scale);
    mappedConstBuffer_->randomTime =
        FiniteOr(randomNoise.time, defaults.randomNoise.time);
    mappedConstBuffer_->randomSeed =
        FiniteOr(randomNoise.seed, defaults.randomNoise.seed);
    mappedConstBuffer_->sceneDimStrength =
        AtLeastFinite(sceneDim.strength, defaults.sceneDim.strength, 0.0f);
    CopyFinite(mappedConstBuffer_->sepiaTone, color.sepiaTone,
               defaults.colorGrade.sepiaTone);
    mappedConstBuffer_->primaryVignetteTintStrength =
        ClampFinite(vignette.primaryTintStrength,
                    defaults.vignette.primaryTintStrength, 0.0f, 1.0f);
    mappedConstBuffer_->secondaryVignetteTintStrength =
        ClampFinite(vignette.secondaryTintStrength,
                    defaults.vignette.secondaryTintStrength, 0.0f, 1.0f);
    CopyFinite(mappedConstBuffer_->primaryVignetteTintColor,
               vignette.primaryTintColor,
               defaults.vignette.primaryTintColor);
    CopyFinite(mappedConstBuffer_->secondaryVignetteTintColor,
               vignette.secondaryTintColor,
               defaults.vignette.secondaryTintColor);
    mappedConstBuffer_->toonEnabled = toon.enabled ? 1 : 0;
    mappedConstBuffer_->toonStrength =
        AtLeastFinite(toon.strength, defaults.toon.strength, 0.0f);
    mappedConstBuffer_->toonColorSteps =
        AtLeastFinite(toon.colorSteps, defaults.toon.colorSteps, 2.0f);
    mappedConstBuffer_->toonEdgeStrength =
        AtLeastFinite(toon.edgeStrength, defaults.toon.edgeStrength, 0.0f);
    mappedConstBuffer_->toonPaddingAlign = 0.0f;
    mappedConstBuffer_->toonPadding[0] = 0.0f;
    mappedConstBuffer_->toonPadding[1] = 0.0f;
    mappedConstBuffer_->toonPadding[2] = 0.0f;
    mappedConstBuffer_->toonPaddingFinal = 0.0f;
    mappedConstBuffer_->constantsPadding[0] = 0.0f;
    mappedConstBuffer_->constantsPadding[1] = 0.0f;
    mappedConstBuffer_->constantsPadding[2] = 0.0f;
    mappedConstBuffer_->constantsPadding[3] = 0.0f;
}

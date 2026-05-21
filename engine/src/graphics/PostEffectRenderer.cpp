#include "graphics/PostEffectRenderer.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include <algorithm>

using namespace DxUtils;

void PostEffectRenderer::Initialize(DirectXCommon *dxCommon,
                                    SrvManager *srvManager, int width,
                                    int height) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;

    CreateRootSignature();
    CreatePipelineState();
    CreateConstantBuffer();
    Resize(width, height);
}

void PostEffectRenderer::Resize(int width, int height) {
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

void PostEffectRenderer::Draw(D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
                              D3D12_GPU_DESCRIPTOR_HANDLE depthHandle) {
    auto commandList = dxCommon_->GetCommandList();

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    commandList->SetDescriptorHeaps(1, heaps);

    commandList->RSSetViewports(1, &viewport_);
    commandList->RSSetScissorRects(1, &scissorRect_);
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetGraphicsRootDescriptorTable(0, textureHandle);
    commandList->SetGraphicsRootDescriptorTable(1, depthHandle);
    commandList->SetGraphicsRootConstantBufferView(
        2, constBuffer_->GetGPUVirtualAddress());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
}

void PostEffectRenderer::SetColorMode(ColorMode mode) {
    colorMode_ = mode;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetFilterMode(FilterMode mode) {
    filterMode_ = mode;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetEdgeMode(EdgeMode mode) {
    edgeMode_ = mode;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetLuminanceEdgeThreshold(float threshold) {
    luminanceEdgeThreshold_ = threshold;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetDepthEdgeThreshold(float threshold) {
    depthEdgeThreshold_ = threshold;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetDepthParameters(float nearZ, float farZ) {
    nearZ_ = nearZ;
    farZ_ = farZ;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetGrayscaleWeights(float r, float g, float b) {
    grayscaleWeights_[0] = r;
    grayscaleWeights_[1] = g;
    grayscaleWeights_[2] = b;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetTonemapEnabled(bool enabled) {
    tonemapEnabled_ = enabled;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetExposure(float exposure) {
    exposure_ = (std::max)(exposure, 0.0f);
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetGamma(float gamma) {
    gamma_ = (std::max)(gamma, 0.001f);
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetBloomEnabled(bool enabled) {
    bloomEnabled_ = enabled;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetBloom(float threshold, float intensity,
                                  float radius) {
    bloomThreshold_ = (std::max)(threshold, 0.0f);
    bloomIntensity_ = (std::max)(intensity, 0.0f);
    bloomRadius_ = (std::max)(radius, 0.0f);
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetNoiseEnabled(bool enabled) {
    noiseEnabled_ = enabled;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetNoise(float strength, float scale) {
    noiseStrength_ = (std::max)(strength, 0.0f);
    noiseScale_ = (std::max)(scale, 0.001f);
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetNoiseTime(float time) {
    noiseTime_ = time;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetSpecialMode(SpecialMode mode) {
    specialMode_ = mode;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetVignette(float strength, float radius) {
    vignetteStrength_ = std::clamp(strength, 0.0f, 1.0f);
    vignetteRadius_ = std::clamp(radius, 0.0f, 1.25f);
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetRadialBlur(float strength) {
    radialBlurStrength_ = std::clamp(strength, 0.0f, 0.18f);
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetDissolve(float amount, float softness,
                                     float scale) {
    dissolveAmount_ = std::clamp(amount, 0.0f, 1.0f);
    dissolveSoftness_ = (std::max)(softness, 0.001f);
    dissolveScale_ = (std::max)(scale, 0.001f);
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetLensFlare(
    bool enabled, float visibility, float sunUvX, float sunUvY, float sunDepth,
    float occlusionBias, float glareRadius, float glareIntensity,
    float glareAlpha, const float glareColor[3], float ghostIntensity,
    float ghostAlpha, const float ghostWarmColor[3],
    const float ghostCoolColor[3], float streakIntensity, float streakAlpha,
    float streakWidth, const float streakColor[3]) {
    lensFlareEnabled_ = enabled;
    lensFlareVisibility_ = std::clamp(visibility, 0.0f, 1.0f);
    lensFlareSunUv_[0] = std::clamp(sunUvX, -0.25f, 1.25f);
    lensFlareSunUv_[1] = std::clamp(sunUvY, -0.25f, 1.25f);
    lensFlareSunDepth_ = std::clamp(sunDepth, 0.0f, 1.0f);
    lensFlareOcclusionBias_ = (std::max)(occlusionBias, 0.0f);
    lensFlareGlareRadius_ = (std::max)(glareRadius, 0.001f);
    lensFlareGlareIntensity_ = (std::max)(glareIntensity, 0.0f);
    lensFlareGlareAlpha_ = std::clamp(glareAlpha, 0.0f, 1.0f);
    lensFlareGhostIntensity_ = (std::max)(ghostIntensity, 0.0f);
    lensFlareGhostAlpha_ = std::clamp(ghostAlpha, 0.0f, 1.0f);
    lensFlareStreakIntensity_ = (std::max)(streakIntensity, 0.0f);
    lensFlareStreakAlpha_ = std::clamp(streakAlpha, 0.0f, 1.0f);
    lensFlareStreakWidth_ = (std::max)(streakWidth, 0.001f);
    for (int i = 0; i < 3; ++i) {
        lensFlareGlareColor_[i] = glareColor[i];
        lensFlareGhostWarmColor_[i] = ghostWarmColor[i];
        lensFlareGhostCoolColor_[i] = ghostCoolColor[i];
        lensFlareStreakColor_[i] = streakColor[i];
    }
    UpdateConstantBuffer();
}

void PostEffectRenderer::CreateRootSignature() {
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
    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "Serialize PostEffect root signature failed");

    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "Create PostEffect root signature failed");
}

void PostEffectRenderer::CreatePipelineState() {
    auto vs =
        ShaderCompiler::Compile(ShaderPaths::PostEffectVS, "main", "vs_5_0");
    auto ps =
        ShaderCompiler::Compile(ShaderPaths::PostEffectPS, "main", "ps_5_0");

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

    ThrowIfFailed(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
                      &desc, IID_PPV_ARGS(&pipelineState_)),
                  "Create PostEffect pipeline state failed");
}

void PostEffectRenderer::CreateConstantBuffer() {
    const UINT size = Align256(sizeof(PostEffectConstants));

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&constBuffer_)),
                  "Create PostEffect constant buffer failed");

    ThrowIfFailed(
        constBuffer_->Map(0, nullptr,
                          reinterpret_cast<void **>(&mappedConstBuffer_)),
        "Map PostEffect constant buffer failed");

    UpdateConstantBuffer();
}

void PostEffectRenderer::UpdateConstantBuffer() {
    if (!mappedConstBuffer_) {
        return;
    }

    mappedConstBuffer_->colorMode = static_cast<int32_t>(colorMode_);
    mappedConstBuffer_->filterMode = static_cast<int32_t>(filterMode_);
    mappedConstBuffer_->texelSize[0] = 1.0f / static_cast<float>(width_);
    mappedConstBuffer_->texelSize[1] = 1.0f / static_cast<float>(height_);
    mappedConstBuffer_->edgeMode = static_cast<int32_t>(edgeMode_);
    mappedConstBuffer_->luminanceEdgeThreshold = luminanceEdgeThreshold_;
    mappedConstBuffer_->depthEdgeThreshold = depthEdgeThreshold_;
    mappedConstBuffer_->nearZ = nearZ_;
    mappedConstBuffer_->farZ = farZ_;
    mappedConstBuffer_->grayscaleWeights[0] = grayscaleWeights_[0];
    mappedConstBuffer_->grayscaleWeights[1] = grayscaleWeights_[1];
    mappedConstBuffer_->grayscaleWeights[2] = grayscaleWeights_[2];
    mappedConstBuffer_->tonemapEnabled = tonemapEnabled_ ? 1 : 0;
    mappedConstBuffer_->exposure = exposure_;
    mappedConstBuffer_->gamma = gamma_;
    mappedConstBuffer_->bloomEnabled = bloomEnabled_ ? 1 : 0;
    mappedConstBuffer_->bloomThreshold = bloomThreshold_;
    mappedConstBuffer_->bloomIntensity = bloomIntensity_;
    mappedConstBuffer_->bloomRadius = bloomRadius_;
    mappedConstBuffer_->noiseEnabled = noiseEnabled_ ? 1 : 0;
    mappedConstBuffer_->noiseStrength = noiseStrength_;
    mappedConstBuffer_->noiseScale = noiseScale_;
    mappedConstBuffer_->noiseTime = noiseTime_;
    mappedConstBuffer_->specialMode = static_cast<int32_t>(specialMode_);
    mappedConstBuffer_->vignetteStrength = vignetteStrength_;
    mappedConstBuffer_->vignetteRadius = vignetteRadius_;
    mappedConstBuffer_->radialBlurStrength = radialBlurStrength_;
    mappedConstBuffer_->dissolveAmount = dissolveAmount_;
    mappedConstBuffer_->dissolveSoftness = dissolveSoftness_;
    mappedConstBuffer_->dissolveScale = dissolveScale_;
    mappedConstBuffer_->lensFlareEnabled = lensFlareEnabled_ ? 1 : 0;
    mappedConstBuffer_->lensFlareVisibility = lensFlareVisibility_;
    mappedConstBuffer_->lensFlareSunUv[0] = lensFlareSunUv_[0];
    mappedConstBuffer_->lensFlareSunUv[1] = lensFlareSunUv_[1];
    mappedConstBuffer_->lensFlareSunDepth = lensFlareSunDepth_;
    mappedConstBuffer_->lensFlareOcclusionBias = lensFlareOcclusionBias_;
    mappedConstBuffer_->lensFlareGlareRadius = lensFlareGlareRadius_;
    mappedConstBuffer_->lensFlareGlareIntensity = lensFlareGlareIntensity_;
    mappedConstBuffer_->lensFlareGhostIntensity = lensFlareGhostIntensity_;
    mappedConstBuffer_->lensFlareStreakIntensity = lensFlareStreakIntensity_;
    mappedConstBuffer_->lensFlareStreakWidth = lensFlareStreakWidth_;
    mappedConstBuffer_->lensFlarePadding0 = 0.0f;
    for (int i = 0; i < 3; ++i) {
        mappedConstBuffer_->lensFlareGlareColor[i] = lensFlareGlareColor_[i];
        mappedConstBuffer_->lensFlareGhostWarmColor[i] =
            lensFlareGhostWarmColor_[i];
        mappedConstBuffer_->lensFlareGhostCoolColor[i] =
            lensFlareGhostCoolColor_[i];
        mappedConstBuffer_->lensFlareStreakColor[i] =
            lensFlareStreakColor_[i];
    }
    mappedConstBuffer_->lensFlareGlareAlpha = lensFlareGlareAlpha_;
    mappedConstBuffer_->lensFlareGhostAlpha = lensFlareGhostAlpha_;
    mappedConstBuffer_->lensFlareStreakAlpha = lensFlareStreakAlpha_;
    mappedConstBuffer_->lensFlarePadding1 = 0.0f;
}

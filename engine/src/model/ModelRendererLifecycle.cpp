#include "internal/ModelRendererInternal.h"
#include "core/Numeric.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "model/ModelRenderer.h"
#include "model/RendererMath.h"
#include "model/Vertex.h"
#include "internal/RendererPipelineVariantUtils.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <vector>

using namespace DirectX;

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;
using GpuResourceHelpers::MapResourceChecked;
using Numeric::AtLeastFinite;
using Numeric::ClampFinite;
using Numeric::FiniteOr;

XMFLOAT4 SanitizeEffectColor(const XMFLOAT4& value, const XMFLOAT4& fallback) {
    return {
        AtLeastFinite(value.x, 0.0f, fallback.x),
        AtLeastFinite(value.y, 0.0f, fallback.y),
        AtLeastFinite(value.z, 0.0f, fallback.z),
        ClampFinite(value.w, 0.0f, 1.0f, fallback.w)};
}

ModelDrawEffect SanitizeDrawEffect(ModelDrawEffect effect) {
    const ModelDrawEffect defaults{};
    effect.color = SanitizeEffectColor(effect.color, defaults.color);
    effect.intensity = AtLeastFinite(effect.intensity, 0.0f, 0.0f);
    effect.fresnelPower = AtLeastFinite(effect.fresnelPower, 0.5f, defaults.fresnelPower);
    effect.noiseAmount = ClampFinite(effect.noiseAmount, 0.0f, 1.0f, defaults.noiseAmount);
    effect.time = FiniteOr(effect.time, defaults.time);
    effect.baseDim = ClampFinite(effect.baseDim, 0.0f, 1.0f, defaults.baseDim);
    effect.alphaBoost = AtLeastFinite(effect.alphaBoost, 0.0f, defaults.alphaBoost);
    effect.surfaceTint = ClampFinite(effect.surfaceTint, 0.0f, 1.0f, defaults.surfaceTint);
    effect.alphaMultiplier =
        ClampFinite(effect.alphaMultiplier, 0.0f, 1.0f, defaults.alphaMultiplier);

    const uint32_t blendOverride = static_cast<uint32_t>(effect.blendOverride);
    if (blendOverride > static_cast<uint32_t>(ModelDrawEffectBlendOverride::Opaque)) {
        effect.blendOverride = ModelDrawEffectBlendOverride::KeepMaterial;
    }
    return effect;
}

std::vector<uint8_t> CreateDissolveNoisePixels(uint32_t width, uint32_t height) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t h = x * 374761393u ^ y * 668265263u ^ 0x8DA6B343u;
            h = (h ^ (h >> 13u)) * 1274126177u;
            h ^= h >> 16u;
            const uint8_t value = static_cast<uint8_t>(h & 0xFFu);
            const size_t index = (static_cast<size_t>(y) * width + x) * 4u;
            pixels[index + 0u] = value;
            pixels[index + 1u] = value;
            pixels[index + 2u] = value;
            pixels[index + 3u] = 255u;
        }
    }
    return pixels;
}

} // namespace

ModelRenderer::ModelRenderer() : state_(std::make_unique<State>()) {}

ModelRenderer::~ModelRenderer() {
    Finalize(true);
}

void ModelRenderer::SetSceneLighting(const SceneLighting& lighting) {
    state_->currentLighting = lighting;
}

void ModelRenderer::SetDrawEffect(const ModelDrawEffect& effect) {
    state_->currentEffect = SanitizeDrawEffect(effect);
}

void ModelRenderer::ClearDrawEffect() {
    state_->currentEffect = ModelDrawEffect{};
}

void ModelRenderer::SetSceneFog(const SceneFog& fog) {
    state_->currentFog = fog;
}

void ModelRenderer::SetEnvironmentTexture(uint32_t textureId) {
    state_->environmentTextureId = textureId;
    state_->hasEnvironmentTexture = true;
}

void ModelRenderer::ClearEnvironmentTexture() {
    state_->hasEnvironmentTexture = false;
}

size_t ModelRenderer::GetUploadBytesPerFrame() const {
    return state_->uploadBuffer.GetBytesPerFrame();
}

size_t ModelRenderer::GetUploadTotalBytes() const {
    return state_->uploadBuffer.GetTotalBytes();
}

size_t ModelRenderer::GetUploadFrameOffset() const {
    return state_->uploadBuffer.GetFrameOffset();
}

void ModelRenderer::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager,
                               MeshManager* meshManager, TextureManager* textureManager,
                               MaterialManager* materialManager) {
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager || !meshManager || !textureManager ||
        !materialManager) {
        Finalize();
        return;
    }

    if (!Finalize()) {
        return;
    }
    state_->dxCommon = dxCommon;
    state_->srvManager = srvManager;
    state_->meshManager = meshManager;
    state_->textureManager = textureManager;
    state_->materialManager = materialManager;
    state_->environmentTextureId = state_->textureManager->GetWhiteCubeTextureId();
    state_->hasEnvironmentTexture = true;
    state_->dissolveNoiseTextureId = state_->textureManager->GetWhiteTextureId();
    state_->shadowMapGpuHandle =
        state_->textureManager->GetGpuHandle(state_->textureManager->GetWhiteTextureId());
    state_->spotLightShadowMapGpuHandle = state_->shadowMapGpuHandle;
    const std::vector<uint8_t> dissolveNoise = CreateDissolveNoisePixels(128u, 128u);
    state_->dissolveNoiseTextureId =
        state_->textureManager->CreateFromRgbaPixels(128u, 128u, dissolveNoise.data());

    CreateRootSignature();
    CreateShadowRootSignature();
    CreateSkinningRootSignature();
    CreatePipelineState();
    CreateShadowPipelineState();
    CreateSkinningPipelineState();
    CreateUploadBuffer();
    CreateIdentityPalette();
    if (!state_->rootSignature || !state_->shadowRootSignature || !state_->skinningRootSignature ||
        !state_->pipelineStates[0] || !state_->instancedPipelineStates[0] || !state_->shadowPSO ||
        !state_->instancedShadowPSO || !state_->skinningPSO ||
        state_->uploadBuffer.GetBytesPerFrame() == 0 || GetIdentityPaletteAddress() == 0) {
        Finalize();
    }
}

bool ModelRenderer::Finalize() {
    return Finalize(false);
}

bool ModelRenderer::Finalize(bool allowFrameAbort) {
    const bool hasGpuResources =
        state_->rootSignature || state_->shadowRootSignature || state_->skinningRootSignature ||
        state_->pipelineStates[0] || state_->instancedPipelineStates[0] || state_->shadowPSO ||
        state_->instancedShadowPSO || state_->skinningPSO || HasIdentityPaletteResources() ||
        state_->uploadBuffer.GetBytesPerFrame() != 0;
    if (!CanReleaseGpuResources(state_->dxCommon, hasGpuResources, allowFrameAbort)) {
        return false;
    }
    if (state_->dxCommon != nullptr) {
        state_->dxCommon->UnregisterFrameRollbacks(this);
    }

    ResetResources();
    return true;
}

bool ModelRenderer::CreateIdentityPalette() {
    ResetIdentityPalette();
    if (state_->dxCommon == nullptr || state_->dxCommon->GetDevice() == nullptr) {
        return false;
    }

    const UINT frameCount = (std::max)(1u, state_->dxCommon->GetSwapChainBufferCount());
    state_->identityPaletteFrames.resize(frameCount);
    WellForGPU identity{};
    identity.skeletonSpaceMatrix = RendererMath::StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));
    identity.skeletonSpaceInverseTransposeMatrix =
        RendererMath::StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto paletteDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(WellForGPU));
    for (SkinPaletteFrame& frame : state_->identityPaletteFrames) {
        if (!CreateCommittedResourceChecked(
                state_->dxCommon->GetDevice(), &uploadHeap, D3D12_HEAP_FLAG_NONE, &paletteDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, frame.resource.GetAddressOf())) {
            ResetIdentityPalette();
            return false;
        }

        void* mapped = nullptr;
        if (!MapResourceChecked(frame.resource.Get(), &mapped)) {
            ResetIdentityPalette();
            return false;
        }
        std::memcpy(mapped, &identity, sizeof(identity));
        frame.resource->Unmap(0, nullptr);
        frame.mappedPalette = nullptr;
    }

    return GetIdentityPaletteAddress() != 0;
}

void ModelRenderer::ResetIdentityPalette() noexcept {
    for (SkinPaletteFrame& frame : state_->identityPaletteFrames) {
        if (frame.resource && frame.mappedPalette != nullptr) {
            frame.resource->Unmap(0, nullptr);
        }
        frame.mappedPalette = nullptr;
        frame.resource.Reset();
    }
    state_->identityPaletteFrames.clear();
}

bool ModelRenderer::HasIdentityPaletteResources() const noexcept {
    return std::any_of(state_->identityPaletteFrames.begin(),
                       state_->identityPaletteFrames.end(),
                       [](const SkinPaletteFrame &frame) {
                           return frame.resource != nullptr;
                       });
}

D3D12_GPU_VIRTUAL_ADDRESS ModelRenderer::GetIdentityPaletteAddress() const {
    if (state_->identityPaletteFrames.empty()) {
        return 0;
    }
    const size_t frameIndex = state_->dxCommon != nullptr ? state_->dxCommon->GetBackBufferIndex() %
                                                                state_->identityPaletteFrames.size()
                                                          : 0;
    if (frameIndex >= state_->identityPaletteFrames.size()) {
        return 0;
    }
    const SkinPaletteFrame& frame = state_->identityPaletteFrames[frameIndex];
    return frame.resource ? frame.resource->GetGPUVirtualAddress() : 0;
}

void ModelRenderer::ResetResources() {
    ResetIdentityPalette();
    state_->dxCommon = nullptr;
    state_->srvManager = nullptr;
    state_->meshManager = nullptr;
    state_->textureManager = nullptr;
    state_->materialManager = nullptr;
    state_->rootSignature.Reset();
    state_->shadowRootSignature.Reset();
    state_->skinningRootSignature.Reset();
    for (auto& pipeline : state_->pipelineStates) {
        pipeline.Reset();
    }
    for (auto& pipeline : state_->instancedPipelineStates) {
        pipeline.Reset();
    }
    state_->shadowPSO.Reset();
    state_->instancedShadowPSO.Reset();
    state_->skinningPSO.Reset();
    state_->uploadBuffer.Reset();
    state_->drawIndex = 0;
    state_->currentGraphicsRootSignature = nullptr;
    state_->currentGraphicsPipelineState = nullptr;
    state_->hasEnvironmentTexture = false;
    state_->shadowMapGpuHandle = {};
    state_->spotLightShadowMapGpuHandle = {};
}

bool ModelRenderer::IsReady() const {
    return state_->dxCommon != nullptr && state_->srvManager != nullptr &&
           state_->meshManager != nullptr && state_->textureManager != nullptr &&
           state_->materialManager != nullptr && state_->rootSignature &&
           state_->shadowRootSignature && state_->skinningRootSignature &&
           RendererPipelineVariantUtils::HasAllPipelineStates(state_->pipelineStates) &&
           RendererPipelineVariantUtils::HasAllPipelineStates(state_->instancedPipelineStates) &&
           state_->shadowPSO && state_->instancedShadowPSO &&
           state_->skinningPSO && GetIdentityPaletteAddress() != 0 &&
           state_->uploadBuffer.GetBytesPerFrame() != 0;
}

void ModelRenderer::BeginFrame() {
    if (!state_->dxCommon) {
        state_->drawIndex = 0;
        return;
    }
    state_->uploadBuffer.BeginFrame(state_->dxCommon->GetBackBufferIndex());
    state_->drawIndex = 0;
    ++state_->skinningFrameId;
    if (state_->skinningFrameId == 0) {
        state_->skinningFrameId = 1;
    }
}

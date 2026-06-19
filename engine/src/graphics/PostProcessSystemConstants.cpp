#include "graphics/PostProcessSystem.h"
#include "internal/PostProcessSystemInternal.h"

#include "core/Numeric.h"
#include "graphics/DirectXCommon.h"
#include "internal/ConstantBufferUtils.h"

#include <algorithm>

namespace {

using Numeric::AtLeastFinite;
using Numeric::ClampFinite;
using Numeric::FiniteOr;

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

} // namespace

void PostProcessSystem::CreateConstantBuffer() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    const UINT frameCount = (std::max)(1u, dxCommon_->GetSwapChainBufferCount());
    if (!ConstantBufferUtils::CreateUploadFrames(
            dxCommon_->GetDevice(), frameCount, sizeof(PostProcessConstants),
            state_->constantFrames, &ConstantFrame::resource,
            &ConstantFrame::mapped)) {
        return;
    }

    UpdateConstantBuffer();
}

PostProcessSystem::ConstantFrame *
PostProcessSystem::GetCurrentConstantFrame() {
    if (state_->constantFrames.empty()) {
        return nullptr;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr
            ? dxCommon_->GetBackBufferIndex() % state_->constantFrames.size()
            : 0;
    return &state_->constantFrames[frameIndex];
}

const PostProcessSystem::ConstantFrame *
PostProcessSystem::GetCurrentConstantFrame() const {
    if (state_->constantFrames.empty()) {
        return nullptr;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr
            ? dxCommon_->GetBackBufferIndex() % state_->constantFrames.size()
            : 0;
    return &state_->constantFrames[frameIndex];
}

bool PostProcessSystem::HasConstantBuffers() const {
    if (state_->constantFrames.empty()) {
        return false;
    }
    return std::all_of(state_->constantFrames.begin(),
                       state_->constantFrames.end(),
                       [](const ConstantFrame &frame) {
                           return frame.resource && frame.mapped != nullptr;
                       });
}

void PostProcessSystem::UpdateConstantBuffer() {
    PostProcessConstants *mappedConstBuffer_ = &state_->constants;

    const auto &color = state_->profile.colorGrade;
    const auto &filter = state_->profile.filter;
    const auto &edge = state_->profile.edge;
    const auto &tonemap = state_->profile.tonemap;
    const auto &bloom = state_->profile.bloom;
    const auto &noise = state_->profile.noise;
    const auto &special = state_->profile.special;
    const auto &vignette = state_->profile.vignette;
    const auto &radialBlur = state_->profile.radialBlur;
    const auto &randomNoise = state_->profile.randomNoise;
    const auto &sceneDim = state_->profile.sceneDim;
    const auto &toon = state_->profile.toon;
    const auto &dissolve = state_->profile.dissolve;
    const auto &lensFlare = state_->profile.lensFlare;
    const PostProcessProfile defaults{};

    float nearZ = AtLeastFinite(edge.nearZ, 0.0001f, defaults.edge.nearZ);
    float farZ = AtLeastFinite(edge.farZ, 0.0002f, defaults.edge.farZ);
    if (farZ <= nearZ) {
        farZ = (std::max)(defaults.edge.farZ, nearZ + 0.0001f);
    }

    mappedConstBuffer_->colorMode =
        ValidModeOrNone(color.mode, 0, static_cast<int32_t>(PostProcessColorMode::Sepia));
    mappedConstBuffer_->filterMode =
        ValidModeOrNone(filter.mode, 0, static_cast<int32_t>(PostProcessFilterMode::GaussianBlur7x7));
    mappedConstBuffer_->texelSize[0] = 1.0f / static_cast<float>(state_->width);
    mappedConstBuffer_->texelSize[1] = 1.0f / static_cast<float>(state_->height);
    mappedConstBuffer_->edgeMode =
        ValidModeOrNone(edge.mode, 0, static_cast<int32_t>(PostProcessEdgeMode::Depth));
    mappedConstBuffer_->luminanceEdgeThreshold =
        AtLeastFinite(edge.luminanceThreshold, 0.0f,
                      defaults.edge.luminanceThreshold);
    mappedConstBuffer_->depthEdgeThreshold =
        AtLeastFinite(edge.depthThreshold, 0.0f,
                      defaults.edge.depthThreshold);
    mappedConstBuffer_->nearZ = nearZ;
    mappedConstBuffer_->farZ = farZ;
    CopyFinite(mappedConstBuffer_->grayscaleWeights, color.grayscaleWeights,
               defaults.colorGrade.grayscaleWeights);
    mappedConstBuffer_->tonemapEnabled = tonemap.enabled ? 1 : 0;
    mappedConstBuffer_->exposure =
        AtLeastFinite(tonemap.exposure, 0.0f, defaults.tonemap.exposure);
    mappedConstBuffer_->gamma =
        AtLeastFinite(tonemap.gamma, 0.0001f, defaults.tonemap.gamma);
    mappedConstBuffer_->bloomEnabled = bloom.enabled ? 1 : 0;
    mappedConstBuffer_->bloomThreshold =
        AtLeastFinite(bloom.threshold, 0.0f, defaults.bloom.threshold);
    mappedConstBuffer_->bloomIntensity =
        AtLeastFinite(bloom.intensity, 0.0f, defaults.bloom.intensity);
    mappedConstBuffer_->bloomRadius =
        AtLeastFinite(bloom.radius, 0.0f, defaults.bloom.radius);
    mappedConstBuffer_->bloomSoftKnee =
        ClampFinite(bloom.softKnee, 0.0f, 1.0f, defaults.bloom.softKnee);
    mappedConstBuffer_->noiseEnabled = noise.enabled ? 1 : 0;
    mappedConstBuffer_->noiseStrength =
        AtLeastFinite(noise.strength, 0.0f, defaults.noise.strength);
    mappedConstBuffer_->noiseScale = FiniteOr(noise.scale, defaults.noise.scale);
    mappedConstBuffer_->noiseTime = FiniteOr(noise.time, defaults.noise.time);
    mappedConstBuffer_->specialMode =
        ValidModeOrNone(special.mode, 0, static_cast<int32_t>(PostProcessSpecialMode::Dissolve));
    mappedConstBuffer_->vignetteStrength =
        AtLeastFinite(vignette.strength, 0.0f, defaults.vignette.strength);
    mappedConstBuffer_->vignetteRadius =
        FiniteOr(vignette.radius, defaults.vignette.radius);
    mappedConstBuffer_->radialBlurStrength =
        AtLeastFinite(radialBlur.strength, 0.0f,
                      defaults.radialBlur.strength);
    mappedConstBuffer_->dissolveAmount =
        ClampFinite(dissolve.amount, 0.0f, 1.0f,
                    defaults.dissolve.amount);
    mappedConstBuffer_->dissolveSoftness =
        AtLeastFinite(dissolve.softness, 0.0001f,
                      defaults.dissolve.softness);
    mappedConstBuffer_->dissolveScale =
        FiniteOr(dissolve.scale, defaults.dissolve.scale);
    mappedConstBuffer_->lensFlareEnabled = lensFlare.enabled ? 1 : 0;
    mappedConstBuffer_->lensFlareVisibility =
        ClampFinite(lensFlare.visibility, 0.0f, 1.0f,
                    defaults.lensFlare.visibility);
    mappedConstBuffer_->lensFlareSourceUv[0] =
        FiniteOr(lensFlare.sourceUv[0], defaults.lensFlare.sourceUv[0]);
    mappedConstBuffer_->lensFlareSourceUv[1] =
        FiniteOr(lensFlare.sourceUv[1], defaults.lensFlare.sourceUv[1]);
    mappedConstBuffer_->lensFlareSourceDepth =
        FiniteOr(lensFlare.sourceDepth, defaults.lensFlare.sourceDepth);
    mappedConstBuffer_->lensFlareOcclusionBias =
        FiniteOr(lensFlare.occlusionBias, defaults.lensFlare.occlusionBias);
    mappedConstBuffer_->lensFlareGlareRadius =
        AtLeastFinite(lensFlare.glareRadius, 0.0001f,
                      defaults.lensFlare.glareRadius);
    mappedConstBuffer_->lensFlareGlareIntensity =
        AtLeastFinite(lensFlare.glareIntensity, 0.0f,
                      defaults.lensFlare.glareIntensity);
    mappedConstBuffer_->lensFlareGhostIntensity =
        AtLeastFinite(lensFlare.ghostIntensity, 0.0f,
                      defaults.lensFlare.ghostIntensity);
    mappedConstBuffer_->lensFlareStreakIntensity =
        AtLeastFinite(lensFlare.streakIntensity, 0.0f,
                      defaults.lensFlare.streakIntensity);
    mappedConstBuffer_->lensFlareStreakWidth =
        AtLeastFinite(lensFlare.streakWidth, 0.0001f,
                      defaults.lensFlare.streakWidth);
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
        ClampFinite(lensFlare.glareAlpha, 0.0f, 1.0f,
                    defaults.lensFlare.glareAlpha);
    mappedConstBuffer_->lensFlareGhostAlpha =
        ClampFinite(lensFlare.ghostAlpha, 0.0f, 1.0f,
                    defaults.lensFlare.ghostAlpha);
    mappedConstBuffer_->lensFlareStreakAlpha =
        ClampFinite(lensFlare.streakAlpha, 0.0f, 1.0f,
                    defaults.lensFlare.streakAlpha);
    mappedConstBuffer_->lensFlareShaftIntensity =
        AtLeastFinite(lensFlare.shaftIntensity, 0.0f,
                      defaults.lensFlare.shaftIntensity);
    mappedConstBuffer_->enableVignetting = vignette.enabled ? 1 : 0;
    mappedConstBuffer_->randomMode =
        ValidModeOrNone(randomNoise.mode, 0, static_cast<int32_t>(PostProcessRandomMode::OverlayNoise));
    mappedConstBuffer_->radialBlurSampleCount =
        std::clamp(radialBlur.sampleCount, 0, 32);
    mappedConstBuffer_->vignettingScale =
        AtLeastFinite(vignette.scale, 0.0f, defaults.vignette.scale);
    mappedConstBuffer_->vignettingPower =
        AtLeastFinite(vignette.power, 0.0001f, defaults.vignette.power);
    mappedConstBuffer_->radialBlurCenter[0] =
        FiniteOr(radialBlur.center[0], defaults.radialBlur.center[0]);
    mappedConstBuffer_->radialBlurCenter[1] =
        FiniteOr(radialBlur.center[1], defaults.radialBlur.center[1]);
    mappedConstBuffer_->randomStrength =
        AtLeastFinite(randomNoise.strength, 0.0f,
                      defaults.randomNoise.strength);
    mappedConstBuffer_->randomScale =
        FiniteOr(randomNoise.scale, defaults.randomNoise.scale);
    mappedConstBuffer_->randomTime =
        FiniteOr(randomNoise.time, defaults.randomNoise.time);
    mappedConstBuffer_->randomSeed =
        FiniteOr(randomNoise.seed, defaults.randomNoise.seed);
    mappedConstBuffer_->sceneDimStrength =
        AtLeastFinite(sceneDim.strength, 0.0f, defaults.sceneDim.strength);
    CopyFinite(mappedConstBuffer_->sepiaTone, color.sepiaTone,
               defaults.colorGrade.sepiaTone);
    mappedConstBuffer_->primaryVignetteTintStrength =
        ClampFinite(vignette.primaryTintStrength, 0.0f, 1.0f,
                    defaults.vignette.primaryTintStrength);
    mappedConstBuffer_->secondaryVignetteTintStrength =
        ClampFinite(vignette.secondaryTintStrength, 0.0f, 1.0f,
                    defaults.vignette.secondaryTintStrength);
    CopyFinite(mappedConstBuffer_->primaryVignetteTintColor,
               vignette.primaryTintColor,
               defaults.vignette.primaryTintColor);
    CopyFinite(mappedConstBuffer_->secondaryVignetteTintColor,
               vignette.secondaryTintColor,
               defaults.vignette.secondaryTintColor);
    mappedConstBuffer_->toonEnabled = toon.enabled ? 1 : 0;
    mappedConstBuffer_->toonStrength =
        AtLeastFinite(toon.strength, 0.0f, defaults.toon.strength);
    mappedConstBuffer_->toonColorSteps =
        AtLeastFinite(toon.colorSteps, 2.0f, defaults.toon.colorSteps);
    mappedConstBuffer_->toonEdgeStrength =
        AtLeastFinite(toon.edgeStrength, 0.0f, defaults.toon.edgeStrength);
    mappedConstBuffer_->toonPaddingAlign = 0.0f;
    mappedConstBuffer_->toonPadding[0] = 0.0f;
    mappedConstBuffer_->toonPadding[1] = 0.0f;
    mappedConstBuffer_->toonPadding[2] = 0.0f;
    mappedConstBuffer_->toonPaddingFinal = 0.0f;
    mappedConstBuffer_->constantsPadding[0] = 0.0f;
    mappedConstBuffer_->constantsPadding[1] = 0.0f;
    mappedConstBuffer_->constantsPadding[2] = 0.0f;
    mappedConstBuffer_->constantsPadding[3] = 0.0f;
    mappedConstBuffer_->constantsPaddingBloom[0] = 0.0f;
    mappedConstBuffer_->constantsPaddingBloom[1] = 0.0f;
    mappedConstBuffer_->constantsPaddingBloom[2] = 0.0f;
}

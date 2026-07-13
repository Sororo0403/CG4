#include "core/Numeric.h"
#include "graphics/DirectXCommon.h"
#include "graphics/PostProcessSystem.h"
#include "internal/ConstantBufferUtils.h"
#include "internal/PostProcessSystemInternal.h"

#include <algorithm>

namespace {

using Numeric::AtLeastFinite;
using Numeric::ClampFinite;
using Numeric::FiniteOr;

template <typename Enum> int32_t ValidModeOrNone(Enum mode, int32_t minValue, int32_t maxValue) {
    const int32_t value = static_cast<int32_t>(mode);
    return value >= minValue && value <= maxValue ? value : 0;
}

template <size_t N>
void CopyFinite(float (&dst)[N], const float (&src)[N], const float (&fallback)[N]) {
    for (size_t i = 0; i < N; ++i) {
        dst[i] = FiniteOr(src[i], fallback[i]);
    }
}

struct EdgeDepthRange {
    float nearZ = 0.1f;
    float farZ = 100.0f;
};

EdgeDepthRange NormalizeEdgeDepthRange(const PostProcessEdgeSettings& edge,
                                       const PostProcessEdgeSettings& defaults) {
    EdgeDepthRange range{};
    range.nearZ = AtLeastFinite(edge.nearZ, 0.0001f, defaults.nearZ);
    range.farZ = AtLeastFinite(edge.farZ, 0.0002f, defaults.farZ);
    if (range.farZ <= range.nearZ) {
        range.farZ = (std::max)(defaults.farZ, range.nearZ + 0.0001f);
    }
    return range;
}

void ApplyColorFilterConstants(PostProcessConstants& constants, const PostProcessProfile& profile,
                               const PostProcessProfile& defaults, int width, int height) {
    constants.colorMode = ValidModeOrNone(profile.colorGrade.mode, 0,
                                          static_cast<int32_t>(PostProcessColorMode::Sepia));
    constants.filterMode = ValidModeOrNone(
        profile.filter.mode, 0, static_cast<int32_t>(PostProcessFilterMode::GaussianBlur7x7));
    constants.texelSize[0] = 1.0f / static_cast<float>(width);
    constants.texelSize[1] = 1.0f / static_cast<float>(height);
    CopyFinite(constants.grayscaleWeights, profile.colorGrade.grayscaleWeights,
               defaults.colorGrade.grayscaleWeights);
    CopyFinite(constants.sepiaTone, profile.colorGrade.sepiaTone, defaults.colorGrade.sepiaTone);
}

void ApplyEdgeConstants(PostProcessConstants& constants, const PostProcessProfile& profile,
                        const PostProcessProfile& defaults) {
    const EdgeDepthRange depthRange = NormalizeEdgeDepthRange(profile.edge, defaults.edge);
    constants.edgeMode =
        ValidModeOrNone(profile.edge.mode, 0, static_cast<int32_t>(PostProcessEdgeMode::Depth));
    constants.luminanceEdgeThreshold =
        AtLeastFinite(profile.edge.luminanceThreshold, 0.0f, defaults.edge.luminanceThreshold);
    constants.depthEdgeThreshold =
        AtLeastFinite(profile.edge.depthThreshold, 0.0f, defaults.edge.depthThreshold);
    constants.nearZ = depthRange.nearZ;
    constants.farZ = depthRange.farZ;
}

void ApplyTonemapBloomConstants(PostProcessConstants& constants, const PostProcessProfile& profile,
                                const PostProcessProfile& defaults) {
    constants.tonemapEnabled = profile.tonemap.enabled ? 1 : 0;
    constants.exposure = AtLeastFinite(profile.tonemap.exposure, 0.0f, defaults.tonemap.exposure);
    constants.gamma = AtLeastFinite(profile.tonemap.gamma, 0.0001f, defaults.tonemap.gamma);
    constants.bloomEnabled = profile.bloom.enabled ? 1 : 0;
    constants.bloomThreshold =
        AtLeastFinite(profile.bloom.threshold, 0.0f, defaults.bloom.threshold);
    constants.bloomIntensity =
        AtLeastFinite(profile.bloom.intensity, 0.0f, defaults.bloom.intensity);
    constants.bloomRadius = AtLeastFinite(profile.bloom.radius, 0.0f, defaults.bloom.radius);
    constants.bloomSoftKnee =
        ClampFinite(profile.bloom.softKnee, 0.0f, 1.0f, defaults.bloom.softKnee);
}

void ApplyNoiseSpecialConstants(PostProcessConstants& constants, const PostProcessProfile& profile,
                                const PostProcessProfile& defaults) {
    constants.noiseEnabled = profile.noise.enabled ? 1 : 0;
    constants.noiseStrength = AtLeastFinite(profile.noise.strength, 0.0f, defaults.noise.strength);
    constants.noiseScale = FiniteOr(profile.noise.scale, defaults.noise.scale);
    constants.noiseTime = FiniteOr(profile.noise.time, defaults.noise.time);
    constants.specialMode = ValidModeOrNone(profile.special.mode, 0,
                                            static_cast<int32_t>(PostProcessSpecialMode::Dissolve));
    constants.dissolveAmount =
        ClampFinite(profile.dissolve.amount, 0.0f, 1.0f, defaults.dissolve.amount);
    constants.dissolveSoftness =
        AtLeastFinite(profile.dissolve.softness, 0.0001f, defaults.dissolve.softness);
    constants.dissolveScale = FiniteOr(profile.dissolve.scale, defaults.dissolve.scale);
}

void ApplyLensFlareConstants(PostProcessConstants& constants, const PostProcessProfile& profile,
                             const PostProcessProfile& defaults) {
    const auto& lensFlare = profile.lensFlare;
    const auto& fallback = defaults.lensFlare;
    constants.lensFlareEnabled = lensFlare.enabled ? 1 : 0;
    constants.lensFlareVisibility =
        ClampFinite(lensFlare.visibility, 0.0f, 1.0f, fallback.visibility);
    constants.lensFlareSourceUv[0] = FiniteOr(lensFlare.sourceUv[0], fallback.sourceUv[0]);
    constants.lensFlareSourceUv[1] = FiniteOr(lensFlare.sourceUv[1], fallback.sourceUv[1]);
    constants.lensFlareSourceDepth = FiniteOr(lensFlare.sourceDepth, fallback.sourceDepth);
    constants.lensFlareOcclusionBias = FiniteOr(lensFlare.occlusionBias, fallback.occlusionBias);
    constants.lensFlareGlareRadius =
        AtLeastFinite(lensFlare.glareRadius, 0.0001f, fallback.glareRadius);
    constants.lensFlareGlareIntensity =
        AtLeastFinite(lensFlare.glareIntensity, 0.0f, fallback.glareIntensity);
    constants.lensFlareGhostIntensity =
        AtLeastFinite(lensFlare.ghostIntensity, 0.0f, fallback.ghostIntensity);
    constants.lensFlareStreakIntensity =
        AtLeastFinite(lensFlare.streakIntensity, 0.0f, fallback.streakIntensity);
    constants.lensFlareStreakWidth =
        AtLeastFinite(lensFlare.streakWidth, 0.0001f, fallback.streakWidth);
    CopyFinite(constants.lensFlareGlareColor, lensFlare.glareColor, fallback.glareColor);
    CopyFinite(constants.lensFlareGhostWarmColor, lensFlare.ghostWarmColor,
               fallback.ghostWarmColor);
    CopyFinite(constants.lensFlareGhostCoolColor, lensFlare.ghostCoolColor,
               fallback.ghostCoolColor);
    CopyFinite(constants.lensFlareStreakColor, lensFlare.streakColor, fallback.streakColor);
    constants.lensFlareGlareAlpha =
        ClampFinite(lensFlare.glareAlpha, 0.0f, 1.0f, fallback.glareAlpha);
    constants.lensFlareGhostAlpha =
        ClampFinite(lensFlare.ghostAlpha, 0.0f, 1.0f, fallback.ghostAlpha);
    constants.lensFlareStreakAlpha =
        ClampFinite(lensFlare.streakAlpha, 0.0f, 1.0f, fallback.streakAlpha);
    constants.lensFlareShaftIntensity =
        AtLeastFinite(lensFlare.shaftIntensity, 0.0f, fallback.shaftIntensity);
}

void ApplyVignetteRandomConstants(PostProcessConstants& constants,
                                  const PostProcessProfile& profile,
                                  const PostProcessProfile& defaults) {
    constants.vignetteStrength =
        AtLeastFinite(profile.vignette.strength, 0.0f, defaults.vignette.strength);
    constants.vignetteRadius = FiniteOr(profile.vignette.radius, defaults.vignette.radius);
    constants.radialBlurStrength =
        AtLeastFinite(profile.radialBlur.strength, 0.0f, defaults.radialBlur.strength);
    constants.enableVignetting = profile.vignette.enabled ? 1 : 0;
    constants.randomMode = ValidModeOrNone(
        profile.randomNoise.mode, 0, static_cast<int32_t>(PostProcessRandomMode::OverlayNoise));
    constants.radialBlurSampleCount = std::clamp(profile.radialBlur.sampleCount, 0, 32);
    constants.vignettingScale =
        AtLeastFinite(profile.vignette.scale, 0.0f, defaults.vignette.scale);
    constants.vignettingPower =
        AtLeastFinite(profile.vignette.power, 0.0001f, defaults.vignette.power);
    constants.radialBlurCenter[0] =
        FiniteOr(profile.radialBlur.center[0], defaults.radialBlur.center[0]);
    constants.radialBlurCenter[1] =
        FiniteOr(profile.radialBlur.center[1], defaults.radialBlur.center[1]);
    constants.randomStrength =
        AtLeastFinite(profile.randomNoise.strength, 0.0f, defaults.randomNoise.strength);
    constants.randomScale = FiniteOr(profile.randomNoise.scale, defaults.randomNoise.scale);
    constants.randomTime = FiniteOr(profile.randomNoise.time, defaults.randomNoise.time);
    constants.randomSeed = FiniteOr(profile.randomNoise.seed, defaults.randomNoise.seed);
    constants.sceneDimStrength =
        AtLeastFinite(profile.sceneDim.strength, 0.0f, defaults.sceneDim.strength);
    constants.primaryVignetteTintStrength = ClampFinite(
        profile.vignette.primaryTintStrength, 0.0f, 1.0f, defaults.vignette.primaryTintStrength);
    constants.secondaryVignetteTintStrength =
        ClampFinite(profile.vignette.secondaryTintStrength, 0.0f, 1.0f,
                    defaults.vignette.secondaryTintStrength);
    CopyFinite(constants.primaryVignetteTintColor, profile.vignette.primaryTintColor,
               defaults.vignette.primaryTintColor);
    CopyFinite(constants.secondaryVignetteTintColor, profile.vignette.secondaryTintColor,
               defaults.vignette.secondaryTintColor);
}

void ApplyToonAndPaddingConstants(PostProcessConstants& constants,
                                  const PostProcessProfile& profile,
                                  const PostProcessProfile& defaults) {
    constants.toonEnabled = profile.toon.enabled ? 1 : 0;
    constants.toonStrength = AtLeastFinite(profile.toon.strength, 0.0f, defaults.toon.strength);
    constants.toonColorSteps =
        AtLeastFinite(profile.toon.colorSteps, 2.0f, defaults.toon.colorSteps);
    constants.toonEdgeStrength =
        AtLeastFinite(profile.toon.edgeStrength, 0.0f, defaults.toon.edgeStrength);
    constants.lensFlarePadding0 = 0.0f;
    constants.lensFlarePadding0b = 0.0f;
    constants.toonPaddingAlign = 0.0f;
    constants.toonPadding[0] = 0.0f;
    constants.toonPadding[1] = 0.0f;
    constants.toonPadding[2] = 0.0f;
    constants.toonPaddingFinal = 0.0f;
    constants.constantsPadding[0] = 0.0f;
    constants.constantsPadding[1] = 0.0f;
    constants.constantsPadding[2] = 0.0f;
    constants.constantsPadding[3] = 0.0f;
    constants.constantsPaddingBloom[0] = 0.0f;
    constants.constantsPaddingBloom[1] = 0.0f;
    constants.constantsPaddingBloom[2] = 0.0f;
}

} // namespace

void PostProcessSystem::CreateConstantBuffer() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    const UINT frameCount = (std::max)(1u, dxCommon_->GetSwapChainBufferCount());
    if (!ConstantBufferUtils::CreateUploadFrames(
            dxCommon_->GetDevice(), frameCount, sizeof(PostProcessConstants),
            state_->constantFrames, &ConstantFrame::resource, &ConstantFrame::mapped)) {
        return;
    }

    UpdateConstantBuffer();
}

PostProcessSystem::ConstantFrame* PostProcessSystem::GetCurrentConstantFrame() {
    if (state_->constantFrames.empty()) {
        return nullptr;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr ? dxCommon_->GetBackBufferIndex() % state_->constantFrames.size() : 0;
    return &state_->constantFrames[frameIndex];
}

const PostProcessSystem::ConstantFrame* PostProcessSystem::GetCurrentConstantFrame() const {
    if (state_->constantFrames.empty()) {
        return nullptr;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr ? dxCommon_->GetBackBufferIndex() % state_->constantFrames.size() : 0;
    return &state_->constantFrames[frameIndex];
}

bool PostProcessSystem::HasConstantBuffers() const {
    if (state_->constantFrames.empty()) {
        return false;
    }
    return std::all_of(
        state_->constantFrames.begin(), state_->constantFrames.end(),
        [](const ConstantFrame& frame) { return frame.resource && frame.mapped != nullptr; });
}

void PostProcessSystem::UpdateConstantBuffer() {
    PostProcessConstants& constants = state_->constants;
    const PostProcessProfile defaults{};
    ApplyColorFilterConstants(constants, state_->profile, defaults, state_->width, state_->height);
    ApplyEdgeConstants(constants, state_->profile, defaults);
    ApplyTonemapBloomConstants(constants, state_->profile, defaults);
    ApplyNoiseSpecialConstants(constants, state_->profile, defaults);
    ApplyLensFlareConstants(constants, state_->profile, defaults);
    ApplyVignetteRandomConstants(constants, state_->profile, defaults);
    ApplyToonAndPaddingConstants(constants, state_->profile, defaults);
}

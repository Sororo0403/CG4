#pragma once
#include <cstdint>

enum class PostProcessColorMode : int32_t {
    None = 0,
    Grayscale = 1,
    Sepia = 2,
};

enum class PostProcessFilterMode : int32_t {
    None = 0,
    Box3x3 = 1,
    Box5x5 = 2,
    Gaussian3x3 = 3,
    GaussianBlur7x7 = 4,
};

enum class PostProcessEdgeMode : int32_t {
    None = 0,
    Luminance = 1,
    Depth = 2,
};

enum class PostProcessSpecialMode : int32_t {
    None = 0,
    Vignette = 1,
    RadialBlur = 2,
    Dissolve = 3,
};

enum class PostProcessRandomMode : int32_t {
    None = 0,
    GrayscaleNoise = 1,
    OverlayNoise = 2,
};

struct PostProcessColorGradeSettings {
    PostProcessColorMode mode = PostProcessColorMode::None;
    float grayscaleWeights[3]{0.2125f, 0.7154f, 0.0721f};
    float sepiaTone[3]{1.20f, 1.00f, 0.80f};
};

struct PostProcessFilterSettings {
    PostProcessFilterMode mode = PostProcessFilterMode::None;
};

struct PostProcessEdgeSettings {
    PostProcessEdgeMode mode = PostProcessEdgeMode::None;
    float luminanceThreshold = 0.2f;
    float depthThreshold = 0.02f;
    float nearZ = 0.1f;
    float farZ = 100.0f;
};

struct PostProcessTonemapSettings {
    bool enabled = false;
    float exposure = 1.0f;
    float gamma = 2.2f;
};

struct PostProcessBloomSettings {
    bool enabled = false;
    float threshold = 1.0f;
    float intensity = 0.25f;
    float radius = 2.0f;
    float softKnee = 0.55f;
    uint32_t maxLevels = 4u;
};

struct PostProcessNoiseSettings {
    bool enabled = false;
    float strength = 0.025f;
    float scale = 240.0f;
    float time = 0.0f;
};

struct PostProcessSpecialSettings {
    PostProcessSpecialMode mode = PostProcessSpecialMode::None;
};

struct PostProcessVignetteSettings {
    bool enabled = false;
    float strength = 0.0f;
    float radius = 0.72f;
    float scale = 16.0f;
    float power = 0.8f;
    float primaryTintStrength = 0.0f;
    float primaryTintColor[3]{1.0f, 1.0f, 1.0f};
    float secondaryTintStrength = 0.0f;
    float secondaryTintColor[3]{1.0f, 1.0f, 1.0f};
};

struct PostProcessRadialBlurSettings {
    float strength = 0.0f;
    float center[2]{0.5f, 0.5f};
    int32_t sampleCount = 10;
};

struct PostProcessRandomNoiseSettings {
    PostProcessRandomMode mode = PostProcessRandomMode::None;
    float strength = 0.0f;
    float scale = 240.0f;
    float time = 0.0f;
    float seed = 0.0f;
};

struct PostProcessSceneDimSettings {
    float strength = 0.0f;
};

struct PostProcessToonSettings {
    bool enabled = false;
    float strength = 0.0f;
    float colorSteps = 5.0f;
    float edgeStrength = 0.0f;
};

struct PostProcessDissolveSettings {
    float amount = 0.0f;
    float softness = 0.08f;
    float scale = 42.0f;
};

struct PostProcessLensFlareSettings {
    bool enabled = false;
    float visibility = 0.0f;
    float sourceUv[2]{0.5f, 0.5f};
    float sourceDepth = 1.0f;
    float occlusionBias = 0.0015f;
    float glareRadius = 0.22f;
    float glareIntensity = 0.0f;
    float glareAlpha = 0.0f;
    float glareColor[3]{1.0f, 0.74f, 0.48f};
    float ghostIntensity = 0.0f;
    float ghostAlpha = 0.0f;
    float ghostWarmColor[3]{1.0f, 0.50f, 0.30f};
    float ghostCoolColor[3]{0.46f, 0.56f, 1.0f};
    float streakIntensity = 0.0f;
    float streakAlpha = 0.0f;
    float streakWidth = 0.018f;
    float streakColor[3]{1.0f, 0.70f, 0.40f};
    float shaftIntensity = 0.0f;
};

struct PostProcessProfile {
    PostProcessColorGradeSettings colorGrade;
    PostProcessFilterSettings filter;
    PostProcessEdgeSettings edge;
    PostProcessTonemapSettings tonemap;
    PostProcessBloomSettings bloom;
    PostProcessNoiseSettings noise;
    PostProcessSpecialSettings special;
    PostProcessVignetteSettings vignette;
    PostProcessRadialBlurSettings radialBlur;
    PostProcessRandomNoiseSettings randomNoise;
    PostProcessSceneDimSettings sceneDim;
    PostProcessToonSettings toon;
    PostProcessDissolveSettings dissolve;
    PostProcessLensFlareSettings lensFlare;
};

struct PostProcessConstants {
    int32_t colorMode = 0;
    int32_t filterMode = 0;
    float texelSize[2]{};
    int32_t edgeMode = 0;
    float luminanceEdgeThreshold = 0.2f;
    float depthEdgeThreshold = 0.02f;
    float nearZ = 0.1f;
    float farZ = 100.0f;
    float grayscaleWeights[3]{0.2125f, 0.7154f, 0.0721f};
    int32_t tonemapEnabled = 0;
    float exposure = 1.0f;
    float gamma = 2.2f;
    int32_t bloomEnabled = 0;
    float bloomThreshold = 1.0f;
    float bloomIntensity = 0.25f;
    float bloomRadius = 2.0f;
    float bloomSoftKnee = 0.55f;
    int32_t noiseEnabled = 0;
    float noiseStrength = 0.025f;
    float noiseScale = 240.0f;
    float noiseTime = 0.0f;
    int32_t specialMode = 0;
    float vignetteStrength = 0.0f;
    float vignetteRadius = 0.72f;
    float radialBlurStrength = 0.0f;
    float dissolveAmount = 0.0f;
    float dissolveSoftness = 0.08f;
    float dissolveScale = 42.0f;
    float postEffectPadding = 0.0f;
    int32_t lensFlareEnabled = 0;
    float lensFlareVisibility = 0.0f;
    float lensFlareSourceUv[2]{0.5f, 0.5f};
    float lensFlareSourceDepth = 1.0f;
    float lensFlareOcclusionBias = 0.0015f;
    float lensFlareGlareRadius = 0.22f;
    float lensFlareGlareIntensity = 0.0f;
    float lensFlareGhostIntensity = 0.0f;
    float lensFlareStreakIntensity = 0.0f;
    float lensFlareStreakWidth = 0.018f;
    float lensFlarePadding0 = 0.0f;
    float lensFlarePadding0b = 0.0f;
    float lensFlareGlareColor[3]{1.0f, 0.74f, 0.48f};
    float lensFlareGlareAlpha = 0.0f;
    float lensFlareGhostWarmColor[3]{1.0f, 0.50f, 0.30f};
    float lensFlareGhostAlpha = 0.0f;
    float lensFlareGhostCoolColor[3]{0.46f, 0.56f, 1.0f};
    float lensFlareStreakAlpha = 0.0f;
    float lensFlareStreakColor[3]{1.0f, 0.70f, 0.40f};
    float lensFlareShaftIntensity = 0.0f;
    int32_t enableVignetting = 0;
    int32_t randomMode = 0;
    int32_t radialBlurSampleCount = 10;
    float vignettingScale = 16.0f;
    float vignettingPower = 0.8f;
    float radialBlurCenter[2]{0.5f, 0.5f};
    float randomStrength = 0.0f;
    float randomScale = 240.0f;
    float randomTime = 0.0f;
    float randomSeed = 0.0f;
    float sceneDimStrength = 0.0f;
    float sepiaTone[3]{1.20f, 1.00f, 0.80f};
    float primaryVignetteTintStrength = 0.0f;
    float primaryVignetteTintColor[3]{1.0f, 1.0f, 1.0f};
    float secondaryVignetteTintStrength = 0.0f;
    float secondaryVignetteTintColor[3]{1.0f, 1.0f, 1.0f};
    int32_t toonEnabled = 0;
    float toonStrength = 0.0f;
    float toonColorSteps = 5.0f;
    float toonEdgeStrength = 0.0f;
    float toonPaddingAlign = 0.0f;
    float toonPadding[3]{};
    float toonPaddingFinal = 0.0f;
    float constantsPadding[4]{};
    float constantsPaddingBloom[3]{};
};

static_assert(sizeof(PostProcessConstants) % 16 == 0,
              "PostProcessConstants must match HLSL cbuffer packing");

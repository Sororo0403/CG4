#pragma once
#include <cstdint>

enum class PostEffectColorMode : int32_t {
    None = 0,
    Grayscale = 1,
};

enum class PostEffectFilterMode : int32_t {
    None = 0,
    Box3x3 = 1,
    Box5x5 = 2,
    Gaussian3x3 = 3,
    GaussianBlur7x7 = 4,
};

enum class PostEffectEdgeMode : int32_t {
    None = 0,
    Luminance = 1,
    Depth = 2,
};

enum class PostEffectSpecialMode : int32_t {
    None = 0,
    Vignette = 1,
    RadialBlur = 2,
    Dissolve = 3,
    SakuraAnime = 4,
};

struct PostEffectConstants {
    int32_t colorMode = 0;
    int32_t filterMode = 0;
    float texelSize[2]{};
    int32_t edgeMode = 0;
    float luminanceEdgeThreshold = 0.2f;
    float depthEdgeThreshold = 0.02f;
    float nearZ = 0.1f;
    float farZ = 100.0f;
    float grayscaleWeights[3]{0.2125f, 0.7154f, 0.0721f};
    int32_t tonemapEnabled = 1;
    float exposure = 1.0f;
    float gamma = 2.2f;
    int32_t bloomEnabled = 0;
    float bloomThreshold = 1.0f;
    float bloomIntensity = 0.25f;
    float bloomRadius = 2.0f;
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
};

#ifndef POST_PROCESS_COMMON_HLSLI
#define POST_PROCESS_COMMON_HLSLI

struct PostProcessVSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

cbuffer PostProcessConstants : register(b0)
{
    int colorMode;
    int filterMode;
    float2 texelSize;
    int edgeMode;
    float luminanceEdgeThreshold;
    float depthEdgeThreshold;
    float nearZ;
    float farZ;
    float3 grayscaleWeights;
    int tonemapEnabled;
    float exposure;
    float gamma;
    int bloomEnabled;
    float bloomThreshold;
    float bloomIntensity;
    float bloomRadius;
    float bloomSoftKnee;
    int noiseEnabled;
    float noiseStrength;
    float noiseScale;
    float noiseTime;
    int specialMode;
    float vignetteStrength;
    float vignetteRadius;
    float radialBlurStrength;
    float dissolveAmount;
    float dissolveSoftness;
    float dissolveScale;
    float postEffectPadding;
    int lensFlareEnabled;
    float lensFlareVisibility;
    float2 lensFlareSourceUv;
    float lensFlareSourceDepth;
    float lensFlareOcclusionBias;
    float lensFlareGlareRadius;
    float lensFlareGlareIntensity;
    float lensFlareGhostIntensity;
    float lensFlareStreakIntensity;
    float lensFlareStreakWidth;
    float lensFlarePadding0;
    float lensFlarePadding0b;
    float3 lensFlareGlareColor;
    float lensFlareGlareAlpha;
    float3 lensFlareGhostWarmColor;
    float lensFlareGhostAlpha;
    float3 lensFlareGhostCoolColor;
    float lensFlareStreakAlpha;
    float3 lensFlareStreakColor;
    float lensFlareShaftIntensity;
    int enableVignetting;
    int randomMode;
    int radialBlurSampleCount;
    float vignettingScale;
    float vignettingPower;
    float2 radialBlurCenter;
    float randomStrength;
    float randomScale;
    float randomTime;
    float randomSeed;
    float sceneDimStrength;
    float3 sepiaTone;
    float primaryVignetteTintStrength;
    float3 primaryVignetteTintColor;
    float secondaryVignetteTintStrength;
    float3 secondaryVignetteTintColor;
    int toonEnabled;
    float toonStrength;
    float toonColorSteps;
    float toonEdgeStrength;
    float toonPaddingAlign;
    float3 toonPadding;
    float toonPaddingFinal;
    float4 constantsPadding;
    float3 constantsPaddingBloom;
};

float CalcLuminance(float3 color)
{
    return dot(color, float3(0.2125f, 0.7154f, 0.0721f));
}

float NoiseHash(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

#endif // POST_PROCESS_COMMON_HLSLI

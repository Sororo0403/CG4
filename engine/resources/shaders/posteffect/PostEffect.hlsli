#ifndef POST_EFFECT_HLSLI
#define POST_EFFECT_HLSLI

struct PostEffectVSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

cbuffer PostEffectConstants : register(b0)
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
};

#endif // POST_EFFECT_HLSLI

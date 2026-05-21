#ifndef SHADOW_SAMPLING_HLSLI
#define SHADOW_SAMPLING_HLSLI

struct ShadowSampleSettings
{
    float filterRadius;
    float depthSoftness;
    float edgeFade;
    float materialStrength;
};

float SampleShadowVisibility(Texture2D<float> shadowMap,
                             SamplerState shadowSampler,
                             float2 shadowUv,
                             float receiverDepth,
                             ShadowSampleSettings settings)
{
    uint shadowWidth = 1;
    uint shadowHeight = 1;
    shadowMap.GetDimensions(shadowWidth, shadowHeight);
    float2 texelSize = 1.0f / float2(max(shadowWidth, 1), max(shadowHeight, 1));
    float sampleRadius = max(settings.filterRadius, 0.0f);
    float depthSoftness = max(settings.depthSoftness, 0.0001f);

    float visibility = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2((float)x, (float)y);
            float weight = (x == 0 && y == 0) ? 4.0f :
                           ((x == 0 || y == 0) ? 2.0f : 1.0f);
            float mapDepth =
                shadowMap.SampleLevel(shadowSampler,
                                      shadowUv + offset * texelSize * sampleRadius,
                                      0).r;
            float sampleVisibility =
                saturate((mapDepth - receiverDepth) * depthSoftness + 0.5f);
            visibility += sampleVisibility * weight;
            weightSum += weight;
        }
    }

    float sampledVisibility = visibility / max(weightSum, 0.0001f);
    float mapEdgeDistance =
        min(min(shadowUv.x, 1.0f - shadowUv.x), min(shadowUv.y, 1.0f - shadowUv.y));
    float mapEdgeFade = smoothstep(0.0f, max(settings.edgeFade, 0.0001f),
                                   mapEdgeDistance);
    sampledVisibility = lerp(1.0f, sampledVisibility, mapEdgeFade);

    return 1.0f - (1.0f - sampledVisibility) *
                      saturate(settings.materialStrength);
}

#endif // SHADOW_SAMPLING_HLSLI

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
    float materialStrength = saturate(settings.materialStrength);
    if (materialStrength <= 0.001f)
    {
        return 1.0f;
    }

    uint shadowWidth = 1;
    uint shadowHeight = 1;
    shadowMap.GetDimensions(shadowWidth, shadowHeight);
    float2 texelSize = 1.0f / float2(max(shadowWidth, 1), max(shadowHeight, 1));
    float sampleRadius = max(settings.filterRadius, 0.0f);
    float depthSoftness = max(settings.depthSoftness, 0.0001f);

    float visibility = 0.0f;
    float weightSum = 0.0f;

    // Keep wide filters dense enough for soft edges without the full 7x7 cost.
    static const int kMaxShadowKernelRadius = 2;
    float supportRadius =
        sampleRadius > 0.01f
            ? min(sampleRadius + 0.5f,
                  (float)kMaxShadowKernelRadius + 0.5f)
            : 0.5f;
    float supportRadiusSq = supportRadius * supportRadius;
    float invSupportRadiusSq = rcp(max(supportRadiusSq, 0.0001f));

    [loop]
    for (int y = -kMaxShadowKernelRadius; y <= kMaxShadowKernelRadius; ++y)
    {
        [loop]
        for (int x = -kMaxShadowKernelRadius; x <= kMaxShadowKernelRadius; ++x)
        {
            float2 offset = float2((float)x, (float)y);
            float distanceSq = dot(offset, offset);
            if (distanceSq > supportRadiusSq)
            {
                continue;
            }

            float falloff = saturate(1.0f - distanceSq * invSupportRadiusSq);
            float weight = falloff * falloff + 0.0005f;
            float mapDepth =
                shadowMap.SampleLevel(shadowSampler,
                                      shadowUv + offset * texelSize, 0).r;
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
                      materialStrength;
}

#endif // SHADOW_SAMPLING_HLSLI

#ifndef FILTER_EFFECT_HLSLI
#define FILTER_EFFECT_HLSLI

#include "BoxFilterEffect.hlsli"
#include "GaussianFilterEffect.hlsli"

float4 ApplyFilterEffect(Texture2D sourceTexture, SamplerState sourceSampler,
                         float2 uv, int mode)
{
    if (mode == 1)
    {
        return SampleBoxFilter(sourceTexture, sourceSampler, uv, texelSize, 1);
    }

    if (mode == 2)
    {
        return SampleBoxFilter(sourceTexture, sourceSampler, uv, texelSize, 2);
    }

    if (mode == 3)
    {
        return SampleGaussianBlur(sourceTexture, sourceSampler, uv, texelSize, 1,
                                  1.0f);
    }

    if (mode == 4)
    {
        return SampleGaussianBlur(sourceTexture, sourceSampler, uv, texelSize, 3,
                                  2.0f);
    }

    return sourceTexture.Sample(sourceSampler, uv);
}

#endif // FILTER_EFFECT_HLSLI

#ifndef BLOOM_PS_HLSLI
#define BLOOM_PS_HLSLI

float3 ApplyBloom(Texture2D bloomTexture, SamplerState sourceSampler,
                  float3 color, float2 uv)
{
    if (bloomEnabled == 0 || bloomIntensity <= 0.0f)
    {
        return color;
    }

    float3 bloom = bloomTexture.Sample(sourceSampler, uv).rgb;
    return color + bloom * bloomIntensity;
}

#endif // BLOOM_PS_HLSLI

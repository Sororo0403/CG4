#ifndef BLOOM_EFFECT_HLSLI
#define BLOOM_EFFECT_HLSLI

float3 ExtractBloom(float3 color)
{
    float luma = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    float amount =
        saturate((luma - bloomThreshold) / max(bloomThreshold, 0.0001f));
    return color * amount;
}

float3 ApplyBloomEffect(Texture2D sourceTexture, SamplerState sourceSampler,
                        float3 color, float2 uv)
{
    if (bloomEnabled == 0)
    {
        return color;
    }

    float2 radius = texelSize * bloomRadius;
    float3 bloom = ExtractBloom(sourceTexture.Sample(sourceSampler, uv).rgb) *
                   0.25f;
    bloom += ExtractBloom(sourceTexture.Sample(sourceSampler,
                                              uv + float2(radius.x, 0.0f))
                              .rgb) *
             0.125f;
    bloom += ExtractBloom(sourceTexture.Sample(sourceSampler,
                                              uv - float2(radius.x, 0.0f))
                              .rgb) *
             0.125f;
    bloom += ExtractBloom(sourceTexture.Sample(sourceSampler,
                                              uv + float2(0.0f, radius.y))
                              .rgb) *
             0.125f;
    bloom += ExtractBloom(sourceTexture.Sample(sourceSampler,
                                              uv - float2(0.0f, radius.y))
                              .rgb) *
             0.125f;
    bloom += ExtractBloom(sourceTexture.Sample(sourceSampler, uv + radius).rgb) *
             0.0625f;
    bloom += ExtractBloom(sourceTexture.Sample(sourceSampler, uv - radius).rgb) *
             0.0625f;
    bloom += ExtractBloom(sourceTexture.Sample(sourceSampler,
                                              uv + float2(radius.x, -radius.y))
                              .rgb) *
             0.0625f;
    bloom += ExtractBloom(sourceTexture.Sample(sourceSampler,
                                              uv + float2(-radius.x, radius.y))
                              .rgb) *
             0.0625f;

    return color + bloom * bloomIntensity;
}

#endif // BLOOM_EFFECT_HLSLI

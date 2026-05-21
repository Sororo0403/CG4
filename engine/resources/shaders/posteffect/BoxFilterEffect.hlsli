#ifndef BOX_FILTER_EFFECT_HLSLI
#define BOX_FILTER_EFFECT_HLSLI

float4 SampleBoxFilter(Texture2D sourceTexture, SamplerState sourceSampler,
                       float2 uv, float2 sampleTexelSize, int radius)
{
    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    const int kernelSize = radius * 2 + 1;
    const float weight = 1.0f / (float)(kernelSize * kernelSize);

    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            const float2 offset = float2(x, y) * sampleTexelSize;
            color += sourceTexture.Sample(sourceSampler, uv + offset) * weight;
        }
    }

    return color;
}

#endif // BOX_FILTER_EFFECT_HLSLI

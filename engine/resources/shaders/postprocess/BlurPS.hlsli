#ifndef BLUR_PS_HLSLI
#define BLUR_PS_HLSLI

float4 SampleBoxFilter(Texture2D sourceTexture, SamplerState sourceSampler,
                       float2 uv, float2 sampleTexelSize, int radius)
{
    float4 color = 0.0f;
    const int kernelSize = radius * 2 + 1;
    const float weight = 1.0f / (float)(kernelSize * kernelSize);

    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            color += sourceTexture.Sample(sourceSampler,
                                          uv + float2(x, y) * sampleTexelSize) *
                     weight;
        }
    }

    return color;
}

float Gaussian(float x, float y, float sigma)
{
    const float twoSigmaSquare = 2.0f * sigma * sigma;
    return exp(-(x * x + y * y) / twoSigmaSquare) /
           (3.14159265f * twoSigmaSquare);
}

float4 SampleGaussianBlur(Texture2D sourceTexture, SamplerState sourceSampler,
                          float2 uv, float2 sampleTexelSize, int radius,
                          float sigma)
{
    float4 color = 0.0f;
    float totalWeight = 0.0f;

    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            const float weight = Gaussian((float)x, (float)y, sigma);
            color += sourceTexture.Sample(sourceSampler,
                                          uv + float2(x, y) * sampleTexelSize) *
                     weight;
            totalWeight += weight;
        }
    }

    return color / totalWeight;
}

float4 ApplyBlur(Texture2D sourceTexture, SamplerState sourceSampler, float2 uv)
{
    if (filterMode == 1)
    {
        return SampleBoxFilter(sourceTexture, sourceSampler, uv, texelSize, 1);
    }
    if (filterMode == 2)
    {
        return SampleBoxFilter(sourceTexture, sourceSampler, uv, texelSize, 2);
    }
    if (filterMode == 3)
    {
        return SampleGaussianBlur(sourceTexture, sourceSampler, uv, texelSize,
                                  1, 1.0f);
    }
    if (filterMode == 4)
    {
        return SampleGaussianBlur(sourceTexture, sourceSampler, uv, texelSize,
                                  3, 2.0f);
    }

    return sourceTexture.Sample(sourceSampler, uv);
}

float4 ApplyRadialBlur(Texture2D sourceTexture, SamplerState sourceSampler,
                       float2 uv, float4 baseColor)
{
    if (radialBlurStrength <= 0.0f || radialBlurSampleCount <= 1)
    {
        return baseColor;
    }

    int sampleCount = min(max(radialBlurSampleCount, 2), 32);
    float2 direction = radialBlurCenter - uv;
    float4 color = sourceTexture.Sample(sourceSampler, uv);

    [loop]
    for (int i = 1; i < 32; ++i)
    {
        if (i >= sampleCount)
        {
            break;
        }
        float percent = (float)i / (float)(sampleCount - 1);
        color += sourceTexture.Sample(sourceSampler,
                                      uv + direction * radialBlurStrength *
                                               percent);
    }

    return color / (float)sampleCount;
}

#endif // BLUR_PS_HLSLI

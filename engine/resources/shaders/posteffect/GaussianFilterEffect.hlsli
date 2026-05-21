#ifndef GAUSSIAN_FILTER_EFFECT_HLSLI
#define GAUSSIAN_FILTER_EFFECT_HLSLI

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
    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            const float weight = Gaussian((float)x, (float)y, sigma);
            const float2 offset = float2(x, y) * sampleTexelSize;
            color += sourceTexture.Sample(sourceSampler, uv + offset) * weight;
            totalWeight += weight;
        }
    }

    return color / totalWeight;
}

#endif // GAUSSIAN_FILTER_EFFECT_HLSLI

#ifndef NOISE_PS_HLSLI
#define NOISE_PS_HLSLI

float3 ApplyNoise(float3 color, float2 uv)
{
    if (noiseEnabled == 0)
    {
        return color;
    }

    float noise = NoiseHash(uv * noiseScale + noiseTime);
    return saturate(color + (noise - 0.5f) * noiseStrength);
}

float Random2dTo1d(float2 value, float seed)
{
    float2 randomVector = float2(12.9898f, 78.233f);
    float random = sin(dot(value, randomVector) + seed * 37.719f);
    return frac(random * 43758.5453f);
}

float4 ApplyRandomNoise(float4 baseColor, float2 uv)
{
    if (randomMode == 0)
    {
        return baseColor;
    }

    float2 cell = floor(uv * max(randomScale, 1.0f));
    float seed = floor(randomTime * 60.0f) + randomSeed;
    float noise = Random2dTo1d(cell, seed);

    if (randomMode == 1)
    {
        return float4(noise.xxx, baseColor.a);
    }

    float grain = (noise - 0.5f) * randomStrength;
    baseColor.rgb = saturate(baseColor.rgb + grain.xxx);
    return baseColor;
}

#endif // NOISE_PS_HLSLI

#ifndef NOISE_EFFECT_HLSLI
#define NOISE_EFFECT_HLSLI

float NoiseHash(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

#endif // NOISE_EFFECT_HLSLI

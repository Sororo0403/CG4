#ifndef TONEMAP_EFFECT_HLSLI
#define TONEMAP_EFFECT_HLSLI

float3 ApplyTonemapEffect(float3 color)
{
    if (tonemapEnabled == 0)
    {
        return saturate(color);
    }

    float3 mapped = 1.0f - exp(-max(color, 0.0f) * exposure);
    return pow(saturate(mapped), 1.0f / gamma);
}

#endif // TONEMAP_EFFECT_HLSLI

#ifndef COLOR_GRADING_PS_HLSLI
#define COLOR_GRADING_PS_HLSLI

float3 ApplyGrayscale(float3 color, float3 weights)
{
    const float value = dot(color, weights);
    return value.xxx;
}

float3 ApplySepia(float3 color, float3 weights, float3 tone)
{
    const float value = dot(color, weights);
    return max(value.xxx * tone, 0.0f);
}

float3 ApplyColorGrading(float3 color)
{
    if (colorMode == 1)
    {
        return ApplyGrayscale(color, grayscaleWeights);
    }

    if (colorMode == 2)
    {
        return ApplySepia(color, grayscaleWeights, sepiaTone);
    }

    return color;
}

float3 ApplyToneMapping(float3 color)
{
    if (tonemapEnabled == 0)
    {
        return color;
    }

    float3 mapped = 1.0f - exp(-max(color, 0.0f) * exposure);
    return pow(saturate(mapped), 1.0f / gamma);
}

#endif // COLOR_GRADING_PS_HLSLI

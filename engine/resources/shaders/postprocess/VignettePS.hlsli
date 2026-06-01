#ifndef VIGNETTE_PS_HLSLI
#define VIGNETTE_PS_HLSLI

float3 ApplySpecialVignette(float3 color, float2 uv)
{
    float distanceFromCenter = distance(uv, float2(0.5f, 0.5f));
    float edge = smoothstep(vignetteRadius, 0.86f, distanceFromCenter);
    return color * (1.0f - edge * vignetteStrength);
}

float3 ApplyVignetting(float3 color, float2 uv)
{
    if (enableVignetting == 0)
    {
        return color;
    }

    float2 correct = uv * (1.0f - uv.yx);
    float vignette = max(correct.x * correct.y * vignettingScale, 0.0f);
    vignette = saturate(pow(vignette, vignettingPower));
    return lerp(color, color * vignette, saturate(vignetteStrength));
}

float EdgeVignetteMask(float2 uv)
{
    float2 edgeUv = abs(uv * 2.0f - 1.0f);
    float edge = smoothstep(0.50f, 1.0f, max(edgeUv.x, edgeUv.y));
    float corner = smoothstep(0.52f, 1.0f, length(edgeUv));
    return pow(saturate(edge * 0.70f + corner * 0.44f), 0.72f);
}

float3 ApplyVignetteTint(float3 color, float2 uv, float strength, float3 tint)
{
    float amount = saturate(strength);
    if (amount <= 0.0001f)
    {
        return color;
    }

    float pulse = amount * EdgeVignetteMask(uv);
    color = lerp(color, tint, pulse * 0.58f);
    color += tint * pulse * 0.30f;
    return saturate(color);
}

float3 ApplyDissolve(float3 color, float2 uv)
{
    float noise = NoiseHash(uv * dissolveScale + noiseTime * 0.17f);
    float mask =
        smoothstep(dissolveAmount - dissolveSoftness,
                   dissolveAmount + dissolveSoftness, noise);
    float edge =
        1.0f - smoothstep(dissolveAmount, dissolveAmount + dissolveSoftness,
                          noise);
    float3 ember = float3(1.0f, 0.58f, 0.18f) * edge;
    return saturate(color * mask + ember);
}

float3 ApplySpecialEffect(float3 color, float2 uv)
{
    if (specialMode == 1)
    {
        return ApplySpecialVignette(color, uv);
    }

    if (specialMode == 3)
    {
        return ApplyDissolve(color, uv);
    }

    return color;
}

#endif // VIGNETTE_PS_HLSLI

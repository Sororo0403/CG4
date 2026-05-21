#include "PostEffect.hlsli"
#include "ColorEffect.hlsli"
#include "EdgeEffect.hlsli"
#include "FilterEffect.hlsli"
#include "BloomEffect.hlsli"
#include "NoiseEffect.hlsli"
#include "TonemapEffect.hlsli"

Texture2D renderTexture : register(t0);
Texture2D depthTexture : register(t1);
SamplerState textureSampler : register(s0);

float3 ApplyNoise(float3 color, float2 uv)
{
    if (noiseEnabled == 0)
    {
        return color;
    }

    float noise = NoiseHash(uv * noiseScale + noiseTime);
    return saturate(color + (noise - 0.5f) * noiseStrength);
}

float4 SampleRadialBlur(float2 uv)
{
    float2 center = float2(0.5f, 0.5f);
    float2 direction = center - uv;
    float4 color = 0.0f;

    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        float amount = ((float)i / 7.0f - 0.5f) * radialBlurStrength;
        color += renderTexture.Sample(textureSampler, uv + direction * amount);
    }

    return color * 0.125f;
}

float3 ApplyVignette(float3 color, float2 uv)
{
    float distanceFromCenter = distance(uv, float2(0.5f, 0.5f));
    float edge = smoothstep(vignetteRadius, 0.86f, distanceFromCenter);
    return color * (1.0f - edge * vignetteStrength);
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

float3 ApplySakuraAnime(float3 color, float2 uv)
{
    float luminance = dot(color, float3(0.2125f, 0.7154f, 0.0721f));
    float3 shadowTint = float3(0.82f, 0.88f, 1.0f);
    float3 highlightTint = float3(1.0f, 0.76f, 0.86f);
    float3 tinted = lerp(color * shadowTint, color * highlightTint,
                         saturate(luminance * 1.15f));

    tinted = pow(saturate(tinted), 0.82f);
    tinted = floor(tinted * 7.0f + 0.5f) / 7.0f;

    float centerLight = 1.0f - smoothstep(0.0f, 0.72f,
                                          distance(uv, float2(0.5f, 0.45f)));
    tinted += float3(0.10f, 0.08f, 0.12f) * centerLight;

    float grain = NoiseHash(uv * 680.0f + noiseTime * 0.11f);
    tinted += (grain - 0.5f) * 0.026f;
    return saturate(tinted);
}

float3 ApplySpecialEffect(float3 color, float2 uv)
{
    if (specialMode == 1)
    {
        return ApplyVignette(color, uv);
    }

    if (specialMode == 3)
    {
        return ApplyDissolve(color, uv);
    }

    if (specialMode == 4)
    {
        return ApplySakuraAnime(color, uv);
    }

    return color;
}

float LensFlareDepthVisibility(Texture2D depthTex, SamplerState samplerState)
{
    if (lensFlareEnabled == 0 || lensFlareVisibility <= 0.001f)
    {
        return 0.0f;
    }

    const float2 offsets[9] =
    {
        float2(0.0f, 0.0f),
        float2(1.5f, 0.0f),
        float2(-1.5f, 0.0f),
        float2(0.0f, 1.5f),
        float2(0.0f, -1.5f),
        float2(3.5f, 2.0f),
        float2(-3.5f, 2.0f),
        float2(3.5f, -2.0f),
        float2(-3.5f, -2.0f),
    };

    float visible = 0.0f;
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        float2 sampleUv = saturate(lensFlareSunUv + offsets[i] * texelSize);
        float sampledDepth = depthTex.Sample(samplerState, sampleUv).r;
        visible += step(lensFlareSunDepth - lensFlareOcclusionBias,
                        sampledDepth);
    }

    return lensFlareVisibility * (visible / 9.0f);
}

float SoftCircle(float2 uv, float2 center, float radius, float power)
{
    float d = distance(uv, center) / max(radius, 0.0001f);
    return pow(saturate(1.0f - d), power);
}

float3 ApplyLensFlare(float3 color, float2 uv)
{
    float visibility = LensFlareDepthVisibility(depthTexture, textureSampler);
    if (visibility <= 0.001f)
    {
        return color;
    }

    float2 center = float2(0.5f, 0.5f);
    float2 sunVector = lensFlareSunUv - center;

    float glare = SoftCircle(uv, lensFlareSunUv, lensFlareGlareRadius, 2.35f);
    float3 flare = lensFlareGlareColor * glare *
                   lensFlareGlareIntensity * lensFlareGlareAlpha;

    const float ghostScales[5] = {0.35f, 0.65f, 1.05f, 1.45f, -0.18f};
    const float ghostRadii[5] = {0.055f, 0.040f, 0.090f, 0.035f, 0.048f};
    const float ghostAlpha[5] = {0.46f, 0.32f, 0.22f, 0.18f, 0.16f};
    const float ghostWarmth[5] = {1.0f, 0.72f, 0.18f, 0.82f, 0.45f};

    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        float2 ghostUv = center - sunVector * ghostScales[i];
        float ghost = SoftCircle(uv, ghostUv, ghostRadii[i], 2.0f);
        float3 ghostColor =
            lerp(lensFlareGhostCoolColor, lensFlareGhostWarmColor,
                 ghostWarmth[i]);
        flare += ghostColor * ghost * ghostAlpha[i] *
                 lensFlareGhostIntensity * lensFlareGhostAlpha;
    }

    float2 delta = uv - lensFlareSunUv;
    float horizontal = exp(-abs(delta.y) / max(lensFlareStreakWidth, 0.0001f));
    horizontal *= smoothstep(0.82f, 0.04f, abs(delta.x));
    float vertical = exp(-abs(delta.x) / max(lensFlareStreakWidth * 0.55f,
                                             0.0001f));
    vertical *= smoothstep(0.52f, 0.02f, abs(delta.y));
    flare += lensFlareStreakColor * (horizontal + vertical * 0.25f) *
             lensFlareStreakIntensity * lensFlareStreakAlpha;

    return saturate(color + flare * visibility);
}

float4 main(PostEffectVSOutput input) : SV_TARGET
{
    float4 outputColor = specialMode == 2
                             ? SampleRadialBlur(input.uv)
                             : ApplyFilterEffect(renderTexture, textureSampler,
                                                 input.uv, filterMode);

    outputColor.rgb =
        ApplyColorEffect(outputColor.rgb, colorMode, grayscaleWeights);

    outputColor = ApplyEdgeEffect(outputColor, renderTexture, depthTexture,
                                  textureSampler, input.uv, edgeMode);

    outputColor.rgb = ApplyBloomEffect(renderTexture, textureSampler,
                                       outputColor.rgb, input.uv);
    outputColor.rgb = ApplyTonemapEffect(outputColor.rgb);
    outputColor.rgb = ApplyNoise(outputColor.rgb, input.uv);
    outputColor.rgb = ApplySpecialEffect(outputColor.rgb, input.uv);
    outputColor.rgb = ApplyLensFlare(outputColor.rgb, input.uv);

    return outputColor;
}

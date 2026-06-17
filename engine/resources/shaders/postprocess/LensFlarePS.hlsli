#ifndef LENS_FLARE_PS_HLSLI
#define LENS_FLARE_PS_HLSLI

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
        float2 sampleUv = saturate(lensFlareSourceUv + offsets[i] * texelSize);
        float sampledDepth = depthTex.Sample(samplerState, sampleUv).r;
        visible += step(lensFlareSourceDepth - lensFlareOcclusionBias,
                        sampledDepth);
    }

    return lensFlareVisibility * (visible / 9.0f);
}

float SoftCircle(float2 uv, float2 center, float radius, float power)
{
    float d = distance(uv, center) / max(radius, 0.0001f);
    return pow(saturate(1.0f - d), power);
}

float SunRayVisibility(Texture2D depthTex, SamplerState samplerState,
                       float2 uv)
{
    float2 rayToSun = lensFlareSourceUv - uv;
    float visible = 0.0f;

    [unroll]
    for (int i = 0; i < 12; ++i)
    {
        float t = (float(i) + 0.5f) / 12.0f;
        float2 sampleUv = saturate(uv + rayToSun * t);
        float sampledDepth = depthTex.Sample(samplerState, sampleUv).r;
        visible += step(lensFlareSourceDepth - lensFlareOcclusionBias,
                        sampledDepth);
    }

    return visible / 12.0f;
}

float3 ApplyLensFlare(Texture2D depthTexture, SamplerState sourceSampler,
                      float3 color, float2 uv)
{
    float visibility = LensFlareDepthVisibility(depthTexture, sourceSampler);
    if (visibility <= 0.001f)
    {
        return color;
    }

    float2 center = float2(0.5f, 0.5f);
    float2 sourceVector = lensFlareSourceUv - center;

    float glare =
        SoftCircle(uv, lensFlareSourceUv, lensFlareGlareRadius, 2.35f);
    float3 flare = lensFlareGlareColor * glare *
                   lensFlareGlareIntensity * lensFlareGlareAlpha;

    const float ghostScales[5] = {0.35f, 0.65f, 1.05f, 1.45f, -0.18f};
    const float ghostRadii[5] = {0.055f, 0.040f, 0.090f, 0.035f, 0.048f};
    const float ghostAlpha[5] = {0.46f, 0.32f, 0.22f, 0.18f, 0.16f};
    const float ghostWarmth[5] = {1.0f, 0.72f, 0.18f, 0.82f, 0.45f};

    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        float2 ghostUv = center - sourceVector * ghostScales[i];
        float ghost = SoftCircle(uv, ghostUv, ghostRadii[i], 2.0f);
        float3 ghostColor =
            lerp(lensFlareGhostCoolColor, lensFlareGhostWarmColor,
                 ghostWarmth[i]);
        flare += ghostColor * ghost * ghostAlpha[i] *
                 lensFlareGhostIntensity * lensFlareGhostAlpha;
    }

    float2 delta = uv - lensFlareSourceUv;
    float horizontal = exp(-abs(delta.y) / max(lensFlareStreakWidth, 0.0001f));
    horizontal *= smoothstep(0.82f, 0.04f, abs(delta.x));
    float vertical = exp(-abs(delta.x) / max(lensFlareStreakWidth * 0.55f,
                                             0.0001f));
    vertical *= smoothstep(0.52f, 0.02f, abs(delta.y));
    flare += lensFlareStreakColor * (horizontal + vertical * 0.25f) *
             lensFlareStreakIntensity * lensFlareStreakAlpha;

    if (lensFlareShaftIntensity > 0.001f)
    {
        float2 rayToSun = lensFlareSourceUv - uv;
        float distanceToSun = length(rayToSun);
        float2 rayDir = rayToSun / max(distanceToSun, 0.0001f);
        float rayFalloff =
            pow(saturate(1.0f - distanceToSun / 0.82f), 2.2f);
        float shaftVisibility =
            SunRayVisibility(depthTexture, sourceSampler, uv);
        float broadBand =
            0.52f + 0.48f *
                        NoiseHash(floor(rayDir * 54.0f +
                                        distanceToSun * 19.0f));
        float fineBand =
            0.88f + 0.12f *
                        sin(dot(uv, float2(91.0f, 47.0f)) +
                            dot(rayDir, float2(33.0f, 17.0f)));
        float shaft = rayFalloff * shaftVisibility * broadBand * fineBand;
        shaft *= smoothstep(0.02f, 0.22f, distanceToSun);
        flare += lensFlareGlareColor * shaft *
                 (lensFlareShaftIntensity * 0.42f);
    }

    return max(color + flare * visibility, 0.0f);
}

#endif // LENS_FLARE_PS_HLSLI

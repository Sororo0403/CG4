struct SkyCloudCommonContext
{
    float4 sunDirectionMode;
    float4 sunColorIntensity;
    float4 weatherParams;
    float4 skyParams;
    float4 groundHorizon;
    float4 sunParams;
    float4 cloudShape;
    float4 cloudLighting;
};

float SkyCloudHash12(float2 value)
{
    float3 p3 = frac(float3(value.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float SkyCloudValueNoise(float2 p)
{
    float2 cell = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);

    float a = SkyCloudHash12(cell);
    float b = SkyCloudHash12(cell + float2(1.0f, 0.0f));
    float c = SkyCloudHash12(cell + float2(0.0f, 1.0f));
    float d = SkyCloudHash12(cell + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float SkyCloudFbmOctaves(float2 p, int octaveCount)
{
    float value = 0.0f;
    float amplitude = 0.52f;
    float total = 0.0f;

    [loop]
    for (int octave = 0; octave < 3; ++octave)
    {
        if (octave >= octaveCount)
        {
            break;
        }
        value += SkyCloudValueNoise(p) * amplitude;
        total += amplitude;
        p = float2(p.x * 1.58f - p.y * 0.42f,
                   p.x * 0.42f + p.y * 1.58f) + float2(13.7f, -8.1f);
        amplitude *= 0.53f;
    }

    return total > 0.001f ? value / total : 0.0f;
}

float SkyCloudFbm(float2 p)
{
    return SkyCloudFbmOctaves(p, 3);
}

float2 SkyCloudFlowDirection(float flowRadians)
{
    return normalize(float2(sin(flowRadians), cos(flowRadians)));
}

float SkyCloudQuality(SkyCloudCommonContext context)
{
    return clamp(context.sunParams.w, 0.25f, 1.0f);
}

float SkyCloudLayerDensity(float3 sampleKm, float baseKm, float thicknessKm,
                           float coverage, float density, float scale,
                           float detailStrength, float crispness,
                           float baseSoftness, float2 flowDirection,
                           float phase, float genusBias, int octaveCount)
{
    float topKm = baseKm + max(thicknessKm, 0.05f);
    float height01 = saturate((sampleKm.y - baseKm) / max(topKm - baseKm, 0.001f));
    float bottomFade = smoothstep(0.0f, max(baseSoftness, 0.025f), height01);
    float topFade = 1.0f - smoothstep(0.72f, 1.0f, height01);
    float vertical = saturate(bottomFade * topFade);

    float2 crossFlow = float2(-flowDirection.y, flowDirection.x);
    float2 wind = flowDirection * phase;
    float2 uv = float2(dot(sampleKm.xz, crossFlow), dot(sampleKm.xz, flowDirection));
    uv = uv * max(scale, 0.15f) + wind;

    float broad = SkyCloudFbmOctaves(uv * 0.34f + genusBias, octaveCount);
    float billow = SkyCloudFbmOctaves(
        uv * 0.92f + broad * 1.45f + genusBias * 3.1f, octaveCount);
    float detail = SkyCloudFbmOctaves(
        uv * 1.75f - billow * 0.75f - genusBias * 1.7f, octaveCount);
    float shape = saturate(broad * 0.52f + billow * 0.42f +
                           detail * detailStrength * 0.06f);

    float threshold = lerp(0.82f, 0.24f, saturate(coverage));
    float edge = lerp(0.30f, 0.055f, saturate(crispness * 0.20f));
    float mask = smoothstep(threshold - edge, threshold + edge, shape);
    float overcast = smoothstep(0.72f, 1.0f, coverage);
    mask = lerp(mask, max(mask, broad * 0.92f), overcast);

    return saturate(mask * vertical * density);
}

float SkyCloudTraceDensity(float3 sampleKm, SkyCloudCommonContext context,
                           float2 flowDirection, float phase)
{
    float coverage = saturate(context.weatherParams.z);
    float density = saturate(context.weatherParams.w);
    if (coverage <= 0.01f || density <= 0.01f)
    {
        return 0.0f;
    }

    float baseKm = clamp(context.skyParams.x, 0.35f, 9.5f);
    float thicknessKm = clamp(context.cloudShape.x, 0.08f, 6.5f);
    float scale = max(context.cloudShape.y, 0.15f);
    float detail = saturate(context.cloudShape.z);
    float crispness = max(context.cloudShape.w, 0.35f);
    float softness = clamp(context.cloudLighting.x, 0.02f, 0.48f);
    float precipitation = saturate(context.skyParams.z);
    float quality = SkyCloudQuality(context);
    int octaveCount = quality < 0.98f ? 2 : 3;
    float volumeCoverage = quality < 0.98f
                               ? saturate(coverage * 0.76f)
                               : coverage;

    float lowWeight = 1.0f - smoothstep(2.0f, 3.0f, baseKm);
    float highWeight = smoothstep(5.0f, 7.0f, baseKm);
    float midWeight = saturate(1.0f - lowWeight - highWeight);
    bool useLow = true;
    bool useMid = true;
    bool useHigh = true;
    if (quality < 0.98f)
    {
        if (lowWeight <= midWeight && lowWeight <= highWeight)
        {
            useLow = false;
        }
        else if (midWeight <= highWeight)
        {
            useMid = false;
        }
        else
        {
            useHigh = false;
        }
    }

    float lowDensity = 0.0f;
    if (useLow)
    {
        lowDensity = SkyCloudLayerDensity(
            sampleKm, clamp(baseKm, 0.45f, 2.4f),
            max(thicknessKm, lerp(0.65f, 3.8f, precipitation)),
            saturate(volumeCoverage * (0.24f + lowWeight * 0.96f) +
                     precipitation * 0.24f),
            density, scale * 1.20f, detail, crispness, softness,
            flowDirection, phase * 0.055f, 1.3f, octaveCount);
    }

    float midDensity = 0.0f;
    if (useMid)
    {
        midDensity = SkyCloudLayerDensity(
            sampleKm, lerp(3.2f, 4.9f, midWeight),
            max(thicknessKm * 0.82f, 0.55f),
            saturate(volumeCoverage * (0.22f + midWeight * 0.88f)),
            density * 0.88f, scale * 0.86f, detail * 0.75f,
            crispness * 0.78f, softness * 1.12f, flowDirection,
            phase * 0.080f, 4.7f, octaveCount);
    }

    float highDensity = 0.0f;
    if (useHigh)
    {
        highDensity = SkyCloudLayerDensity(
            sampleKm, lerp(7.2f, 8.8f, highWeight),
            max(thicknessKm * 0.34f, 0.16f),
            saturate(volumeCoverage * (0.12f + highWeight * 0.84f) +
                     volumeCoverage * (1.0f - density) * 0.12f),
            density * 0.44f, scale * 0.54f, detail * 0.42f,
            crispness * 0.55f, softness * 1.45f, flowDirection,
            phase * 0.13f, 8.9f, octaveCount);
    }

    return saturate(max(max(lowDensity, midDensity), highDensity));
}

float SkyCloudSunTransmittance(float3 sampleKm, float3 sunDir,
                               SkyCloudCommonContext context,
                               float2 flowDirection, float phase)
{
    float quality = SkyCloudQuality(context);
    if (quality < 0.98f)
    {
        return 0.88f;
    }
    int sampleCount = 3;
    float opticalDepth = 0.0f;
    float stepKm = max(context.cloudShape.x, 0.25f) * 0.18f;

    [loop]
    for (int i = 1; i <= 3; ++i)
    {
        if (i > sampleCount)
        {
            break;
        }
        float3 p = sampleKm + sunDir * (stepKm * (float)i);
        opticalDepth += SkyCloudTraceDensity(p, context, flowDirection, phase);
    }

    float shadowStrength = saturate(context.cloudLighting.z * 0.55f);
    return exp(-opticalDepth * (0.48f + shadowStrength * 1.15f));
}

float3 SkyCloudShade(float3 skyColor, float3 viewDir, float3 sampleKm,
                     float localDensity, SkyCloudCommonContext context,
                     float2 flowDirection, float phase)
{
    float3 sunDir = normalize(context.sunDirectionMode.xyz);
    float sunAmount = saturate(dot(viewDir, sunDir));
    float sunTransmittance =
        SkyCloudSunTransmittance(sampleKm, sunDir, context, flowDirection, phase);
    float daylight = smoothstep(-0.055f, 0.08f, sunDir.y);
    float twilight = smoothstep(-0.22f, 0.025f, sunDir.y) * (1.0f - daylight);
    float height01 = saturate((sampleKm.y - 1.0f) / 8.0f);

    float forward = pow(sunAmount, lerp(10.0f, 58.0f, height01));
    float silver = forward * context.cloudLighting.y * sunTransmittance;
    float underside = 1.0f - smoothstep(0.16f, 0.56f, saturate(viewDir.y));

    float3 warmTwilight = float3(1.0f, 0.43f, 0.20f) *
                          twilight * lerp(0.38f, 0.10f, height01);
    float3 ambient = lerp(float3(0.34f, 0.37f, 0.44f),
                          float3(0.86f, 0.88f, 0.91f), daylight);
    ambient = lerp(ambient, skyColor * 0.72f + context.groundHorizon.rgb * 0.16f,
                   0.24f);
    float3 sunLit = context.sunColorIntensity.rgb *
                    (0.74f + sunTransmittance * 0.82f + silver * 0.62f);
    float3 cloudColor = lerp(ambient, sunLit,
                             saturate(sunTransmittance * (0.46f + daylight * 0.44f)));
    cloudColor += warmTwilight;
    cloudColor += context.sunColorIntensity.rgb * silver * 0.34f;
    cloudColor = lerp(cloudColor, float3(0.30f, 0.32f, 0.36f),
                      underside * context.cloudLighting.z * localDensity * 0.28f);
    cloudColor = lerp(cloudColor, float3(0.24f, 0.26f, 0.29f),
                      saturate(context.skyParams.z) * underside * 0.34f);
    return max(cloudColor, 0.0f);
}

float3 SkyCloudApplyVolumetric(float3 skyColor, float3 viewDir,
                               SkyCloudCommonContext context)
{
    float coverage = saturate(context.weatherParams.z);
    float density = saturate(context.weatherParams.w);
    if (coverage <= 0.01f || density <= 0.01f || viewDir.y <= 0.006f)
    {
        return skyColor;
    }
    float quality = SkyCloudQuality(context);
    if (quality < 0.98f && (coverage < 0.18f || viewDir.y < 0.16f))
    {
        return skyColor;
    }

    float baseKm = clamp(context.skyParams.x, 0.35f, 9.5f);
    float thicknessKm = clamp(context.cloudShape.x, 0.08f, 6.5f);
    float topKm = quality < 0.98f
                      ? baseKm + max(thicknessKm * 2.20f, 1.55f)
                      : max(baseKm + thicknessKm, 9.1f);
    float originYKm = 0.003f;
    float enterT = max((min(baseKm, 1.1f) - originYKm) / max(viewDir.y, 0.012f), 0.0f);
    float exitT = max((topKm - originYKm) / max(viewDir.y, 0.012f), enterT + 0.01f);
    exitT = min(exitT, 96.0f);

    float2 flowDirection = SkyCloudFlowDirection(context.cloudLighting.w);
    float phase = context.skyParams.w * context.skyParams.y;
    float distanceKm = max(exitT - enterT, 0.01f);
    int sampleCount = quality < 0.98f ? 3 : 6;
    float stepKm = distanceKm / (float)sampleCount;
    float alpha = 0.0f;
    float3 scattered = 0.0f;
    float jitter = 0.5f;

    [loop]
    for (int stepIndex = 0; stepIndex < 6; ++stepIndex)
    {
        if (stepIndex >= sampleCount)
        {
            break;
        }
        float t = enterT + (float(stepIndex) + jitter) * stepKm;
        float3 sampleKm = viewDir * t;
        float localDensity =
            SkyCloudTraceDensity(sampleKm, context, flowDirection, phase);
        if (localDensity <= 0.001f)
        {
            continue;
        }

        float optical = localDensity * stepKm *
                        (0.50f + density * 0.92f + context.cloudShape.x * 0.05f);
        float sampleAlpha = saturate((1.0f - exp(-optical)) * (1.0f - alpha));
        float3 cloudColor = SkyCloudShade(skyColor, viewDir, sampleKm,
                                          localDensity, context,
                                          flowDirection, phase);
        scattered += cloudColor * sampleAlpha;
        alpha += sampleAlpha;
        if (alpha >= 0.965f)
        {
            break;
        }
    }

    float horizonFade = smoothstep(0.20f, 0.48f, viewDir.y);
    alpha *= horizonFade * horizonFade;
    return lerp(skyColor, scattered / max(alpha, 0.001f), saturate(alpha));
}

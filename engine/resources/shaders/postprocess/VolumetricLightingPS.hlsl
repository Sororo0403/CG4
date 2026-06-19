#include "../common/ShadowSampling.hlsli"

struct PostProcessVSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

Texture2D<float> depthTexture : register(t0);
Texture2D<float> shadowMap : register(t1);
SamplerState depthSampler : register(s0);
SamplerState shadowSampler : register(s1);

cbuffer VolumetricLightingConstants : register(b0)
{
    float4 cameraPositionNearFar;
    float4 sunDirectionIntensity;
    float4 sunColorExtinction;
    float4 volumeParams0;
    float4 volumeParams1;
    float4 shadowParams;
    float4 renderParams;
    float4x4 inverseViewProjection;
    float4x4 lightViewProjection;
};

static const float kPi = 3.14159265358979323846f;

float NoiseHash(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float3 ReconstructWorldPosition(float2 uv, float deviceDepth)
{
    float4 clip = float4(uv.x * 2.0f - 1.0f,
                         (1.0f - uv.y) * 2.0f - 1.0f,
                         deviceDepth, 1.0f);
    float4 world = mul(clip, inverseViewProjection);
    return world.xyz / max(abs(world.w), 0.00001f);
}

float PhaseHenyeyGreenstein(float cosTheta, float g)
{
    float gg = g * g;
    float denom = max(1.0f + gg - 2.0f * g * cosTheta, 0.0001f);
    return (1.0f - gg) / (4.0f * kPi * pow(denom, 1.5f));
}

float HeightDensity(float3 worldPos)
{
    float baseY = volumeParams1.x;
    float falloffMeters = max(volumeParams1.y, 0.25f);
    float aboveBase = max(worldPos.y - baseY, 0.0f);
    float density = exp(-aboveBase / falloffMeters) * volumeParams0.w;
    float noiseStrength = saturate(volumeParams1.z);
    if (noiseStrength > 0.0001f)
    {
        float2 noiseUv = worldPos.xz * 0.037f +
                         float2(volumeParams1.w * 0.018f,
                                -volumeParams1.w * 0.013f);
        float noise = NoiseHash(noiseUv);
        density *= lerp(1.0f - noiseStrength, 1.0f + noiseStrength,
                        noise);
    }
    return max(density, 0.0f);
}

float SampleLightVisibility(float3 worldPos)
{
    if (shadowParams.y <= 0.0001f)
    {
        return 1.0f;
    }

    float4 shadowClip = mul(float4(worldPos, 1.0f), lightViewProjection);
    shadowClip.xyz /= max(abs(shadowClip.w), 0.00001f);
    float2 shadowUv = shadowClip.xy * float2(0.5f, -0.5f) + 0.5f;
    if (!all(shadowUv >= 0.0f) || !all(shadowUv <= 1.0f) ||
        shadowClip.z < 0.0f || shadowClip.z > 1.0f)
    {
        return 1.0f;
    }

    ShadowSampleSettings sampleSettings;
    sampleSettings.filterRadius = shadowParams.z;
    sampleSettings.depthSoftness = max(shadowParams.w, 1.0f);
    sampleSettings.edgeFade = max(renderParams.w, 0.0f);
    sampleSettings.materialStrength = saturate(shadowParams.y);
    return SampleShadowVisibility(shadowMap, shadowSampler, shadowUv,
                                  shadowClip.z - shadowParams.x,
                                  sampleSettings);
}

float4 main(PostProcessVSOutput input) : SV_TARGET
{
    float rawDepth = depthTexture.Sample(depthSampler, input.uv);
    float3 cameraPos = cameraPositionNearFar.xyz;
    float maxDistance = max(volumeParams0.z, 0.5f);
    float3 depthWorld = ReconstructWorldPosition(input.uv, min(rawDepth, 0.99999f));
    float3 viewVector = depthWorld - cameraPos;
    float depthDistance = length(viewVector);
    if (depthDistance <= cameraPositionNearFar.w)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float3 rayDir = viewVector / max(depthDistance, 0.0001f);
    float marchDistance =
        rawDepth >= 0.9999f ? maxDistance : min(depthDistance, maxDistance);
    float sampleCount = clamp(renderParams.z, 1.0f, 48.0f);
    float stepLength = marchDistance / sampleCount;
    if (stepLength <= 0.0001f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float3 sunDir = normalize(sunDirectionIntensity.xyz);
    float cosTheta = dot(rayDir, sunDir);
    float phase = PhaseHenyeyGreenstein(cosTheta, saturate(volumeParams0.y));
    float3 sunRadiance = sunColorExtinction.rgb * sunDirectionIntensity.w;
    float baseExtinction = max(sunColorExtinction.w, 0.0f);
    float scatteringAlbedo = saturate(volumeParams0.x);

    float dither = NoiseHash(input.pos.xy + volumeParams1.w);
    float distanceT = stepLength * (0.35f + dither * 0.45f);
    float transmittance = 1.0f;
    float3 radiance = float3(0.0f, 0.0f, 0.0f);
    float previousVisibility = 1.0f;

    [loop]
    for (int i = 0; i < 48; ++i)
    {
        if (i >= (int)sampleCount || transmittance < 0.01f)
        {
            break;
        }

        float3 samplePos = cameraPos + rayDir * distanceT;
        float density = HeightDensity(samplePos);
        float extinction = baseExtinction * density;
        if (extinction > 0.0000001f)
        {
            float stepTransmittance = exp(-extinction * stepLength);
            float segment = (1.0f - stepTransmittance) / extinction;
            float visibility =
                (i == 0 || (i & 1) == 0)
                    ? SampleLightVisibility(samplePos)
                    : previousVisibility;
            previousVisibility = visibility;
            float distanceFade = 1.0f - saturate(distanceT / maxDistance);
            float scattering = extinction * scatteringAlbedo;
            float singleScatter = phase * visibility;
            float multipleScatter =
                (0.020f + distanceFade * 0.035f) *
                lerp(0.55f, 1.0f, scatteringAlbedo) *
                lerp(0.45f, 1.0f, visibility);
            radiance +=
                transmittance * sunRadiance * scattering *
                (singleScatter + multipleScatter) * segment *
                (0.35f + distanceFade * 0.65f);
            transmittance *= stepTransmittance;
        }

        distanceT += stepLength;
    }

    return float4(max(radiance, 0.0f), 0.0f);
}

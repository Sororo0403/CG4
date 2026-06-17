#ifndef MESH_LIGHTING_HLSLI
#define MESH_LIGHTING_HLSLI

#include "../common/ShadowSampling.hlsli"

float3 ApplyNormalMap(Texture2D normalTexture, SamplerState normalSampler,
                      int normalMapEnabled, float normalMapStrength,
                      float3 vertexNormal, float4 vertexTangent, float2 uv)
{
    float3 normal = normalize(vertexNormal);
    if (normalMapEnabled == 0)
    {
        return normal;
    }

    float3 tangent = vertexTangent.xyz - normal * dot(normal, vertexTangent.xyz);
    float tangentLength = length(tangent);
    if (tangentLength < 0.0001f)
    {
        return normal;
    }
    tangent /= tangentLength;

    float3 bitangent = normalize(cross(normal, tangent) * vertexTangent.w);
    float3 sampledNormal = normalTexture.Sample(normalSampler, uv).xyz * 2.0f - 1.0f;
    sampledNormal.xy *= max(normalMapStrength, 0.0f);
    sampledNormal = normalize(sampledNormal);
    return normalize(sampledNormal.x * tangent +
                     sampledNormal.y * bitangent +
                     sampledNormal.z * normal);
}

float WrappedDiffuse(float3 normal, float3 lightDir, float wrap)
{
    return saturate((saturate(dot(normal, lightDir)) + wrap) /
                    (1.0f + wrap));
}

float3 AccumulatePointLight(PointLight light, float3 worldPos, float3 normal)
{
    float3 lightVector = light.positionRange.xyz - worldPos;
    float lightDistance = length(lightVector);
    if (lightDistance <= 0.0001f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 lightDir = lightVector / lightDistance;
    float attenuation =
        saturate(1.0f - lightDistance / max(light.positionRange.w, 0.001f));
    attenuation *= attenuation;
    return light.colorIntensity.rgb * saturate(dot(normal, lightDir)) *
           attenuation * light.colorIntensity.w;
}

float3 AccumulatePointLights(PointLight point0, PointLight point1,
                             float3 worldPos, float3 normal)
{
    return AccumulatePointLight(point0, worldPos, normal) +
           AccumulatePointLight(point1, worldPos, normal);
}

float3 AccumulateSpotLight(SpotLight spotLight, float3 worldPos, float3 normal,
                           float3 viewDir, float specularPower,
                           float specularStrength)
{
    if (spotLight.angleParams.w <= 0.5f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 spotVector = spotLight.positionRange.xyz - worldPos;
    float spotDistance = length(spotVector);
    if (spotDistance <= 0.0001f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 toLightDir = spotVector / spotDistance;
    float3 fromLightDir = -toLightDir;
    float coneCos = dot(fromLightDir, normalize(spotLight.direction.xyz));
    float cone = saturate((coneCos - spotLight.angleParams.y) /
                          max(spotLight.angleParams.x - spotLight.angleParams.y,
                              0.0001f));
    cone = pow(cone, max(spotLight.angleParams.z, 0.0001f));
    float rangeAttenuation =
        saturate(1.0f - spotDistance / max(spotLight.positionRange.w, 0.001f));
    rangeAttenuation *= rangeAttenuation;
    float spotDiffuse = saturate((dot(normal, toLightDir) + 0.32f) / 1.32f);
    float spotSpecular =
        pow(saturate(dot(normal, normalize(toLightDir + viewDir))),
            specularPower) *
        specularStrength;
    return spotLight.colorIntensity.rgb *
           (spotDiffuse + spotSpecular * 0.72f) * cone * rangeAttenuation *
           spotLight.colorIntensity.w;
}

float SampleDirectShadowVisibility(Texture2D<float> shadowMap,
                                   SamplerState shadowSampler,
                                   float3 worldPos, float3 normal,
                                   float4x4 lightViewProjection,
                                   float4 shadowParams,
                                   float4 shadowFilterParams,
                                   float materialShadowStrength,
                                   float filterScale)
{
    if (shadowParams.x <= 0.5f)
    {
        return 1.0f;
    }

    float3 shadowWorldPos = worldPos + normal * shadowParams.w;
    float4 shadowClip = mul(float4(shadowWorldPos, 1.0f), lightViewProjection);
    shadowClip.xyz /= max(shadowClip.w, 0.0001f);
    float2 shadowUv = shadowClip.xy * float2(0.5f, -0.5f) + 0.5f;
    if (!all(shadowUv >= 0.0f) || !all(shadowUv <= 1.0f) ||
        shadowClip.z < 0.0f || shadowClip.z > 1.0f)
    {
        return 1.0f;
    }

    ShadowSampleSettings sampleSettings;
    sampleSettings.filterRadius = shadowFilterParams.x * max(filterScale, 0.0f);
    sampleSettings.depthSoftness = shadowFilterParams.y;
    sampleSettings.edgeFade = shadowFilterParams.z;
    sampleSettings.materialStrength = saturate(materialShadowStrength);
    return SampleShadowVisibility(shadowMap, shadowSampler, shadowUv,
                                  shadowClip.z - shadowParams.y,
                                  sampleSettings);
}

float SampleSpotShadowVisibility(Texture2D<float> shadowMap,
                                 SamplerState shadowSampler,
                                 float3 worldPos, float3 normal,
                                 float4x4 lightViewProjection,
                                 float4 shadowParams,
                                 float4 shadowFilterParams,
                                 float materialShadowStrength,
                                 float filterScale)
{
    return SampleDirectShadowVisibility(
        shadowMap, shadowSampler, worldPos, normal, lightViewProjection,
        shadowParams, shadowFilterParams, materialShadowStrength, filterScale);
}

float3 ApplyDirectShadowToLighting(float3 lighting, float3 ambient,
                                   float visibility)
{
    return ambient + (lighting - ambient) * visibility;
}

float3 ApplyAerialPerspectiveFog(float3 surfaceColor, float3 worldPos,
                                 float3 cameraWorldPos, float3 sunDirection,
                                 float3 sunColor, float3 ambientLight,
                                 float4 fogColor, float4 fogParams)
{
    if (fogParams.x <= 0.5f)
    {
        return surfaceColor;
    }

    float3 viewVector = worldPos - cameraWorldPos;
    float viewDistance = length(viewVector);
    if (viewDistance <= 0.0001f)
    {
        return surfaceColor;
    }

    float3 viewDir = viewVector / viewDistance;
    float fogRange = max(fogParams.z - fogParams.y, 0.0001f);
    float fogAmount = saturate((viewDistance - fogParams.y) / fogRange);
    fogAmount = pow(fogAmount, max(fogParams.w, 0.0001f));

    float horizon = 1.0f - saturate(abs(viewDir.y) * 2.35f);
    float heightFalloff =
        exp(-max(worldPos.y - cameraWorldPos.y, 0.0f) * 0.018f);
    float lowAir = lerp(0.72f, 1.18f, horizon * heightFalloff);

    float sunLength = length(sunDirection);
    float3 sunDir = sunLength > 0.001f
                        ? sunDirection / sunLength
                        : float3(0.0f, 1.0f, 0.0f);
    float sunForward = pow(saturate(dot(viewDir, sunDir) * 0.5f + 0.5f),
                           7.0f);
    float3 airLight =
        fogColor.rgb +
        ambientLight * (0.12f + horizon * 0.14f) +
        sunColor * sunForward * (0.035f + fogColor.a * 0.090f);

    float airAmount = saturate(fogAmount * fogColor.a * lowAir);
    return lerp(surfaceColor, max(airLight, 0.0f), airAmount);
}

#endif // MESH_LIGHTING_HLSLI

#include "Model.hlsli"
#include "../common/ShadowSampling.hlsli"

Texture2D tex0 : register(t0);
TextureCube<float4> gEnvironmentTexture : register(t2);
Texture2D<float> gShadowMap : register(t3);
Texture2D normalMap : register(t4);
Texture2D<float4> gDissolveNoiseTexture : register(t5);
SamplerState samp0 : register(s0);

cbuffer ObjectTransform : register(b0)
{
    float4x4 matWVP;
    float4x4 matWorld;
    float4x4 matWorldInverseTranspose;
};

cbuffer SceneParams : register(b1)
{
    float4 cameraPos;
    float4 keyLightDirection;
    float4 keyLightColor;
    float4 fillLightDirection;
    float4 fillLightColor;
    float4 ambientColor;
    PointLight pointLights[2];
    float4 lightingParams;
    float4 lightingModeParams;
    float4 fogColor;
    float4 fogParams;
    float4x4 viewProjection;
    float4x4 lightViewProjection;
    float4 shadowParams;
    float4 shadowFilterParams;
    SpotLight spotLight;
};

cbuffer Material : register(b2)
{
    float4 color;
    float4x4 uvTransform;
    int enableTexture;
    float reflectionStrength;
    float reflectionFresnelStrength;
    float reflectionRoughness;
    int blendMode;
    float alphaCutoff;
    int cullMode;
    int depthWrite;
    float roughness;
    float metallic;
    float normalStrength;
    int enableNormalMap;
    float4 customParams;
    float4 customParams2;
    float4 customParams3;
};

cbuffer DrawEffect : register(b3)
{
    float4 drawEffectColor;
    float4 drawEffectParams0;
    float4 drawEffectParams1;
    float4 drawEffectParams2;
};

float3 ApplyNormalMap(float3 vertexNormal, float4 vertexTangent, float2 uv)
{
    float3 normal = normalize(vertexNormal);
    if (enableNormalMap == 0)
    {
        return normal;
    }

    float3 tangent = normalize(vertexTangent.xyz - normal * dot(normal, vertexTangent.xyz));
    float tangentLength = length(tangent);
    if (tangentLength < 0.0001f)
    {
        return normal;
    }

    float3 bitangent = normalize(cross(normal, tangent) * vertexTangent.w);
    float3 sampledNormal = normalMap.Sample(samp0, uv).xyz * 2.0f - 1.0f;
    sampledNormal.xy *= max(normalStrength, 0.0f);
    sampledNormal = normalize(sampledNormal);
    return normalize(sampledNormal.x * tangent +
                     sampledNormal.y * bitangent +
                     sampledNormal.z * normal);
}

float4 main(ModelVSOutput input) : SV_TARGET
{
    float2 uv = mul(float4(input.uv, 0.0f, 1.0f), uvTransform).xy;

    float4 texColor = float4(1, 1, 1, 1);
    if (enableTexture != 0)
    {
        if (customParams2.x > 0.5f)
        {
            float rustScale = max(customParams2.y, 0.001f);
            float3 p = input.bindPos * rustScale;
            float3 n = abs(normalize(input.worldNormal));
            n = pow(n, 4.0f);
            n /= max(n.x + n.y + n.z, 0.0001f);

            float4 sampleX = tex0.Sample(samp0, p.zy);
            float4 sampleY = tex0.Sample(samp0, p.xz);
            float4 sampleZ = tex0.Sample(samp0, p.xy);
            texColor = sampleX * n.x + sampleY * n.y + sampleZ * n.z;
        }
        else
        {
            texColor = tex0.Sample(samp0, uv);
        }
    }

    float4 finalColor = texColor * color * input.color;
    if (blendMode == 1 && finalColor.a < alphaCutoff)
    {
        discard;
    }
    if (drawEffectParams1.w > 0.5f)
    {
        finalColor.a = 1.0f;
    }
    float alphaMultiplier = saturate(drawEffectParams2.y);

    float dissolveEdgeRate = 0.0f;
    if (customParams.x > 0.5f)
    {
        float dissolveNoise = gDissolveNoiseTexture.Sample(samp0, uv).r;
        float dripNoise =
            gDissolveNoiseTexture.Sample(samp0, uv * float2(0.65f, 3.2f)).r;
        float verticalMelt =
            saturate(1.0f - uv.y + (dripNoise - 0.5f) * 0.42f);
        dissolveNoise = saturate(dissolveNoise * 0.72f + verticalMelt * 0.28f);
        float dissolveAmount = dissolveNoise - saturate(customParams.y);
        clip(dissolveAmount);

        float edgeWidth = max(customParams.z, 0.0001f);
        dissolveEdgeRate = 1.0f - smoothstep(0.0f, edgeWidth, dissolveAmount);
    }

    float3 normal = ApplyNormalMap(input.worldNormal, input.worldTangent, uv);
    float3 viewDir = normalize(cameraPos.xyz - input.worldPos);

    float3 keyDir = normalize(-keyLightDirection.xyz);
    float3 fillDir = normalize(-fillLightDirection.xyz);

    float keyDiffuse = saturate(dot(normal, keyDir));
    float fillDiffuse = saturate(dot(normal, fillDir));
    float wrap = saturate(lightingParams.w);
    keyDiffuse = saturate((keyDiffuse + wrap) / (1.0f + wrap));
    fillDiffuse = saturate((fillDiffuse + wrap * 0.5f) / (1.0f + wrap * 0.5f));

    float specularPower = max(lightingParams.x, 1.0f);
    float specularStrength = saturate(lightingParams.y);
    float blinnSpecular = pow(saturate(dot(normal, normalize(keyDir + viewDir))), specularPower);
    float phongSpecular = pow(saturate(dot(viewDir, reflect(-keyDir, normal))), specularPower);
    float useBlinnPhong = lightingModeParams.x > 0.5f ? 1.0f : 0.0f;
    float keySpecular = lerp(phongSpecular, blinnSpecular, useBlinnPhong) * specularStrength;

    float rimPower = max(lightingParams.z, 0.5f);
    float rim = pow(saturate(1.0f - dot(normal, viewDir)), rimPower);

    float3 pointAccum = float3(0.0f, 0.0f, 0.0f);

    float3 point0Vector = pointLights[0].positionRange.xyz - input.worldPos;
    float point0Distance = length(point0Vector);
    if (point0Distance > 0.0001f)
    {
        float3 point0Dir = point0Vector / point0Distance;
        float point0Attenuation = saturate(1.0f - point0Distance / max(pointLights[0].positionRange.w, 0.001f));
        point0Attenuation *= point0Attenuation;
        float point0Diffuse = saturate(dot(normal, point0Dir));
        pointAccum += pointLights[0].colorIntensity.rgb * point0Diffuse *
                      point0Attenuation * pointLights[0].colorIntensity.w;
    }

    float3 point1Vector = pointLights[1].positionRange.xyz - input.worldPos;
    float point1Distance = length(point1Vector);
    if (point1Distance > 0.0001f)
    {
        float3 point1Dir = point1Vector / point1Distance;
        float point1Attenuation = saturate(1.0f - point1Distance / max(pointLights[1].positionRange.w, 0.001f));
        point1Attenuation *= point1Attenuation;
        float point1Diffuse = saturate(dot(normal, point1Dir));
        pointAccum += pointLights[1].colorIntensity.rgb * point1Diffuse *
                      point1Attenuation * pointLights[1].colorIntensity.w;
    }

    float3 spotAccum = float3(0.0f, 0.0f, 0.0f);
    if (spotLight.angleParams.w > 0.5f)
    {
        float3 spotVector = spotLight.positionRange.xyz - input.worldPos;
        float spotDistance = length(spotVector);
        if (spotDistance > 0.0001f)
        {
            float3 toLightDir = spotVector / spotDistance;
            float3 fromLightDir = -toLightDir;
            float coneCos = dot(fromLightDir, normalize(spotLight.direction.xyz));
            float cone = saturate((coneCos - spotLight.angleParams.y) /
                                  max(spotLight.angleParams.x - spotLight.angleParams.y, 0.0001f));
            cone = pow(cone, max(spotLight.angleParams.z, 0.0001f));
            float rangeAttenuation =
                saturate(1.0f - spotDistance / max(spotLight.positionRange.w, 0.001f));
            rangeAttenuation *= rangeAttenuation;
            float spotDiffuse =
                saturate((dot(normal, toLightDir) + 0.32f) / 1.32f);
            float spotSpecular =
                pow(saturate(dot(normal, normalize(toLightDir + viewDir))),
                    specularPower) * specularStrength;
            spotAccum += spotLight.colorIntensity.rgb *
                         (spotDiffuse + spotSpecular * 0.72f) *
                         cone * rangeAttenuation * spotLight.colorIntensity.w;
        }
    }

    float3 lighting =
        ambientColor.rgb +
        keyLightColor.rgb * keyDiffuse +
        fillLightColor.rgb * fillDiffuse +
        pointAccum +
        spotAccum +
        keyLightColor.rgb * keySpecular +
        fillLightColor.rgb * rim * fillLightColor.a;

    if (shadowParams.x > 0.5f)
    {
        float3 shadowWorldPos = input.worldPos + normal * shadowParams.w;
        float4 shadowClip = mul(float4(shadowWorldPos, 1.0f), lightViewProjection);
        shadowClip.xyz /= max(shadowClip.w, 0.0001f);
        float2 shadowUv = shadowClip.xy * float2(0.5f, -0.5f) + 0.5f;
        if (all(shadowUv >= 0.0f) && all(shadowUv <= 1.0f) &&
            shadowClip.z >= 0.0f && shadowClip.z <= 1.0f)
        {
            float receiverDepth = shadowClip.z - shadowParams.y;
            float materialShadowStrength =
                blendMode == 1 ? saturate(shadowParams.z) * 0.45f : saturate(shadowParams.z);
            const float shadowSoftness = blendMode == 1 ? 0.9f : 0.45f;
            ShadowSampleSettings sampleSettings;
            sampleSettings.filterRadius =
                shadowFilterParams.x * lerp(0.85f, 1.25f, saturate(shadowSoftness));
            sampleSettings.depthSoftness = shadowFilterParams.y;
            sampleSettings.edgeFade = shadowFilterParams.z;
            sampleSettings.materialStrength = materialShadowStrength;
            float shadowVisibility = SampleShadowVisibility(
                gShadowMap, samp0, shadowUv, receiverDepth, sampleSettings);
            lighting = ambientColor.rgb + (lighting - ambientColor.rgb) * shadowVisibility;
        }
    }

    finalColor.rgb *= lighting;

    float environmentStrength =
        reflectionStrength + rim * reflectionFresnelStrength;
    if (environmentStrength > 0.0001f)
    {
        float3 reflectedVector = reflect(-viewDir, normal);
        uint envWidth = 0;
        uint envHeight = 0;
        uint envMipLevels = 1;
        gEnvironmentTexture.GetDimensions(0, envWidth, envHeight, envMipLevels);
        float maxMipLevel = max((float)envMipLevels - 1.0f, 0.0f);
        float mipLevel = saturate(reflectionRoughness) * maxMipLevel;
        float3 environmentColor =
            gEnvironmentTexture.SampleLevel(samp0, reflectedVector, mipLevel).rgb;
        finalColor.rgb += environmentColor * environmentStrength;
    }

    finalColor.rgb =
        lerp(finalColor.rgb, customParams3.rgb,
             dissolveEdgeRate * customParams3.a);

    float effectEnabled = drawEffectParams0.x;
    float effectIntensity = drawEffectParams0.y;
    if (effectEnabled > 0.5f && effectIntensity > 0.0001f)
    {
        float fresnelPower = max(drawEffectParams0.z, 0.5f);
        float noiseAmount = saturate(drawEffectParams0.w);
        float time = drawEffectParams1.x;
        float baseDim = saturate(drawEffectParams1.y);
        float alphaBoost = max(drawEffectParams1.z, 0.0f);
        float surfaceTint = saturate(drawEffectParams2.x);

        float effectRim = pow(saturate(1.0f - abs(dot(normal, viewDir))),
                              fresnelPower);

        float noise =
            sin(input.worldPos.x * 10.0f + time * 15.0f) *
            sin(input.worldPos.y * 12.0f - time * 11.0f) *
            sin(input.worldPos.z * 9.0f + time * 17.0f);
        noise = lerp(1.0f, 0.65f + 0.35f * noise, noiseAmount);

        float pulse = 0.8f + 0.2f * sin(time * 20.0f + input.worldPos.y * 8.0f);
        float glow = effectRim * noise * pulse * effectIntensity;

        finalColor.rgb *= lerp(1.0f, 0.24f, baseDim);
        float3 effectShadowTint = lerp(float3(0.46f, 0.40f, 0.34f),
                                       saturate(drawEffectColor.rgb), 0.28f);
        finalColor.rgb = lerp(finalColor.rgb, finalColor.rgb * effectShadowTint,
                              baseDim * 0.55f);

        if (surfaceTint > 0.0001f)
        {
            float surfaceMix = saturate(surfaceTint * drawEffectColor.a);
            finalColor.rgb = lerp(finalColor.rgb, drawEffectColor.rgb, surfaceMix);
            finalColor.a = saturate(max(finalColor.a * (1.0f - surfaceTint * 0.45f),
                                        drawEffectColor.a * surfaceTint));
        }

        finalColor.rgb += drawEffectColor.rgb * glow;
        finalColor.a =
            saturate(finalColor.a + drawEffectColor.a * glow * alphaBoost);
    }

    finalColor.a *= alphaMultiplier;

    if (fogParams.x > 0.5f)
    {
        float viewDistance = distance(cameraPos.xyz, input.worldPos);
        float fogRange = max(fogParams.z - fogParams.y, 0.0001f);
        float fogAmount = saturate((viewDistance - fogParams.y) / fogRange);
        fogAmount = pow(fogAmount, max(fogParams.w, 0.0001f));
        finalColor.rgb = lerp(finalColor.rgb, fogColor.rgb, fogAmount * fogColor.a);
    }

    return finalColor;
}

#include "Model.hlsli"
#include "../common/ShadowSampling.hlsli"

Texture2D tex0 : register(t0);
TextureCube<float4> gEnvironmentTexture : register(t2);
Texture2D<float> gShadowMap : register(t3);
Texture2D normalMap : register(t4);
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
        texColor = tex0.Sample(samp0, uv);
    }

    float4 finalColor = texColor * color * input.color;
    if (blendMode == 1 && finalColor.a < alphaCutoff)
    {
        discard;
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

    float3 lighting =
        ambientColor.rgb +
        keyLightColor.rgb * keyDiffuse +
        fillLightColor.rgb * fillDiffuse +
        pointAccum +
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

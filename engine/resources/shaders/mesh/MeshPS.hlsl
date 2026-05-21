#include "Mesh.hlsli"
#include "../common/ShadowSampling.hlsli"

Texture2D tex0 : register(t0);
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
    float4 fogColor;
    float4 fogParams;
    float4x4 viewProjection;
    float4x4 lightViewProjection;
    float4 shadowParams;
    float4 shadowFilterParams;
    float4 customSceneParams0;
    float4 customSceneParams1;
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

float4 main(MeshVSOutput input) : SV_TARGET
{
    float2 uv = mul(float4(input.uv, 0.0f, 1.0f), uvTransform).xy;

    float4 texColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    if (enableTexture != 0)
    {
        texColor = tex0.Sample(samp0, uv);
    }

    float4 finalColor = color * input.color;
    if (enableTexture != 0)
    {
        float3 vertexTint = lerp(float3(1.0f, 1.0f, 1.0f), input.color.rgb, 0.55f);
        finalColor.rgb = texColor.rgb * color.rgb * vertexTint;
        finalColor.a = texColor.a * color.a * input.color.a;
    }
    float fadeAlpha = saturate(input.color.a);
    if (blendMode != 2 && fadeAlpha < Dither01(input.worldPos, uv))
    {
        discard;
    }

    if (blendMode == 1)
    {
        float shapeAlpha = texColor.a * color.a;
        if (shapeAlpha < alphaCutoff)
        {
            discard;
        }
        finalColor.a = shapeAlpha * input.color.a;
        finalColor.rgb = lerp(color.rgb * input.color.rgb, finalColor.rgb, 0.72f);
    }

    float3 viewDir = normalize(cameraPos.xyz - input.worldPos);
    float3 normal = ApplyNormalMap(input.worldNormal, input.worldTangent, uv);
    if (cullMode == 0 && dot(normal, viewDir) < 0.0f)
    {
        normal = -normal;
    }
    float3 keyDir = normalize(-keyLightDirection.xyz);
    float3 fillDir = normalize(-fillLightDirection.xyz);

    float keyDiffuse = saturate(dot(normal, keyDir));
    float fillDiffuse = saturate(dot(normal, fillDir));
    float wrap = saturate(lightingParams.w);
    keyDiffuse = saturate((keyDiffuse + wrap) / (1.0f + wrap));
    fillDiffuse = saturate((fillDiffuse + wrap * 0.5f) / (1.0f + wrap * 0.5f));

    float3 halfVector = normalize(keyDir + viewDir);
    float specularPower = max(lightingParams.x, 1.0f);
    float specularStrength = saturate(lightingParams.y);
    float keySpecular = pow(saturate(dot(normal, halfVector)), specularPower) * specularStrength;

    float rimPower = max(lightingParams.z, 0.5f);
    float rim = pow(saturate(1.0f - dot(normal, viewDir)), rimPower);

    float3 pointAccum = float3(0.0f, 0.0f, 0.0f);

    float3 point0Vector = pointLights[0].positionRange.xyz - input.worldPos;
    float point0Distance = length(point0Vector);
    if (point0Distance > 0.0001f)
    {
        float3 point0Dir = point0Vector / point0Distance;
        float attenuation = saturate(1.0f - point0Distance / max(pointLights[0].positionRange.w, 0.001f));
        attenuation *= attenuation;
        pointAccum += pointLights[0].colorIntensity.rgb *
                      saturate(dot(normal, point0Dir)) * attenuation *
                      pointLights[0].colorIntensity.w;
    }

    float3 point1Vector = pointLights[1].positionRange.xyz - input.worldPos;
    float point1Distance = length(point1Vector);
    if (point1Distance > 0.0001f)
    {
        float3 point1Dir = point1Vector / point1Distance;
        float attenuation = saturate(1.0f - point1Distance / max(pointLights[1].positionRange.w, 0.001f));
        attenuation *= attenuation;
        pointAccum += pointLights[1].colorIntensity.rgb *
                      saturate(dot(normal, point1Dir)) * attenuation *
                      pointLights[1].colorIntensity.w;
    }

    float3 lighting =
        ambientColor.rgb +
        keyLightColor.rgb * keyDiffuse +
        fillLightColor.rgb * fillDiffuse +
        pointAccum +
        keyLightColor.rgb * keySpecular +
        fillLightColor.rgb * rim * fillLightColor.a;

    float directShadowVisibility = 1.0f;
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
            float materialShadowStrength = saturate(shadowParams.z);
            if (blendMode == 1)
            {
                materialShadowStrength *= 0.35f;
            }

            ShadowSampleSettings sampleSettings;
            sampleSettings.filterRadius = shadowFilterParams.x;
            sampleSettings.depthSoftness = shadowFilterParams.y;
            sampleSettings.edgeFade = shadowFilterParams.z;
            sampleSettings.materialStrength = materialShadowStrength;
            directShadowVisibility = SampleShadowVisibility(
                gShadowMap, samp0, shadowUv, receiverDepth, sampleSettings);
            lighting = ambientColor.rgb + (lighting - ambientColor.rgb) * directShadowVisibility;
        }
    }

    finalColor.rgb *= lighting;

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

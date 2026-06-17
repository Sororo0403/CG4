#include "Mesh.hlsli"
#include "MeshLighting.hlsli"
#include "../common/PbrLighting.hlsli"

Texture2D tex0 : register(t0);
TextureCube<float4> gEnvironmentTexture : register(t2);
Texture2D<float> gShadowMap : register(t3);
Texture2D normalMap : register(t4);
Texture2D<float> gSpotLightShadowMap : register(t6);
Texture2D roughnessMap : register(t7);
Texture2D metallicMap : register(t8);
SamplerState samp0 : register(s0);
SamplerState shadowSamp : register(s1);

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
    SpotLight spotLight;
    float4x4 spotLightViewProjection;
    float4 spotShadowParams;
    float4 spotShadowFilterParams;
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
    float4 pbrTextureParams;
};

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
    if (blendMode != 2 && fadeAlpha < Dither01(input.ditherPos, uv))
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

    float3 viewDir = PbrNormalizeOrDefault(
        cameraPos.xyz - input.worldPos, float3(0.0f, 0.0f, 1.0f));
    float3 shadowNormal = PbrNormalizeOrDefault(input.worldNormal,
                                                float3(0.0f, 1.0f, 0.0f));
    if (cullMode == 0 && dot(shadowNormal, viewDir) < 0.0f)
    {
        shadowNormal = -shadowNormal;
    }
    float3 normal = ApplyNormalMap(normalMap, samp0, enableNormalMap,
                                   normalStrength, input.worldNormal,
                                   input.worldTangent, uv);
    if (cullMode == 0 && dot(normal, viewDir) < 0.0f)
    {
        normal = -normal;
    }
    PbrMaterialValues pbrMaterial =
        PbrSampleMaterial(roughnessMap, metallicMap, samp0, uv, roughness,
                          metallic, pbrTextureParams);
    float materialAo = pbrMaterial.ambientOcclusion;
    float materialRoughness = pbrMaterial.roughness;
    float materialMetallic = pbrMaterial.metallic;
    float3 albedo = saturate(finalColor.rgb);
    float3 keyDir = PbrNormalizeOrDefault(
        -keyLightDirection.xyz, float3(0.45f, 0.78f, 0.34f));
    float3 fillDir = PbrNormalizeOrDefault(
        -fillLightDirection.xyz, float3(-0.42f, 0.62f, -0.36f));
    float wrap = saturate(lightingParams.w);
    float hazeArea = saturate(lightingParams.y);
    float keyLightArea = lerp(0.010f, 0.062f, hazeArea);
    float fillLightArea = lerp(0.145f, 0.235f, hazeArea);

    float3 directLighting =
        PbrEvaluateDirectArea(albedo, normal, viewDir, keyDir,
                              keyLightColor.rgb, materialRoughness,
                              materialMetallic, wrap, keyLightArea) +
        PbrEvaluateDirectArea(albedo, normal, viewDir, fillDir,
                              fillLightColor.rgb * fillLightColor.a,
                              materialRoughness, materialMetallic,
                              wrap * 0.5f, fillLightArea);
    float3 pointAccum = PbrAccumulatePointLights(
        pointLights[0], pointLights[1], albedo, input.worldPos, normal,
        viewDir, materialRoughness, materialMetallic, wrap);
    float3 spotAccum =
        PbrAccumulateSpotLight(spotLight, albedo, input.worldPos, normal,
                               viewDir, materialRoughness, materialMetallic,
                               wrap);
    if (spotShadowParams.x > 0.5f)
    {
        float materialSpotShadowStrength = saturate(spotShadowParams.z);
        if (blendMode == 1)
        {
            materialSpotShadowStrength *= 0.40f;
        }
        spotAccum *= SampleSpotShadowVisibility(
            gSpotLightShadowMap, shadowSamp, input.worldPos, shadowNormal,
            spotLightViewProjection, spotShadowParams, spotShadowFilterParams,
            materialSpotShadowStrength, 1.0f);
    }
    directLighting += pointAccum + spotAccum;

    float directShadowVisibility = 1.0f;
    if (shadowParams.x > 0.5f)
    {
        float materialShadowStrength = saturate(shadowParams.z);
        if (blendMode == 1)
        {
            materialShadowStrength *= 0.35f;
        }
        directShadowVisibility = SampleDirectShadowVisibility(
            gShadowMap, shadowSamp, input.worldPos, shadowNormal, lightViewProjection,
            shadowParams, shadowFilterParams, materialShadowStrength, 1.0f);
    }

    float3 ambientLighting =
        PbrEvaluateAmbientDiffuse(albedo, ambientColor.rgb, materialAo);
    float ambientSpecularScale =
        saturate(reflectionStrength + reflectionFresnelStrength * 0.55f);
    float3 ambientSpecular = float3(0.0f, 0.0f, 0.0f);
    if (ambientSpecularScale > 0.0001f)
    {
        float3 reflectedVector =
            PbrNormalizeOrDefault(reflect(-viewDir, normal), normal);
        float environmentRoughness =
            saturate(max(materialRoughness, reflectionRoughness));
        float3 environmentRadiance =
            PbrSampleEnvironment(gEnvironmentTexture, samp0, reflectedVector,
                                 environmentRoughness);
        environmentRadiance = lerp(ambientColor.rgb, environmentRadiance, 0.74f);
        ambientSpecular =
            PbrEvaluateAmbientSpecular(albedo, normal, viewDir,
                                       environmentRadiance, materialRoughness,
                                       materialMetallic, materialAo,
                                       ambientSpecularScale);
    }
    finalColor.rgb =
        ambientLighting + ambientSpecular + directLighting * directShadowVisibility;

    finalColor.rgb = ApplyAerialPerspectiveFog(
        finalColor.rgb, input.worldPos, cameraPos.xyz, -keyLightDirection.xyz,
        keyLightColor.rgb, ambientColor.rgb, fogColor, fogParams);

    return finalColor;
}

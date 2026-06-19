#include "Model.hlsli"
#include "../mesh/MeshLighting.hlsli"
#include "../common/PbrLighting.hlsli"

Texture2D tex0 : register(t0);
TextureCube<float4> gEnvironmentTexture : register(t2);
Texture2D<float> gShadowMap : register(t3);
Texture2D normalMap : register(t4);
Texture2D<float4> gDissolveNoiseTexture : register(t5);
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
    float4 lightingModeParams;
    float4 fogColor;
    float4 fogParams;
    float4x4 viewProjection;
    float4x4 lightViewProjection;
    float4 shadowParams;
    float4 shadowFilterParams;
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

cbuffer DrawEffect : register(b3)
{
    float4 drawEffectColor;
    float4 drawEffectParams0;
    float4 drawEffectParams1;
    float4 drawEffectParams2;
};

float4 main(ModelVSOutput input) : SV_TARGET
{
    float2 uv = mul(float4(input.uv, 0.0f, 1.0f), uvTransform).xy;

    float4 texColor = float4(1, 1, 1, 1);
    if (enableTexture != 0)
    {
        if (customParams2.x > 0.5f)
        {
            float rustScale = max(customParams2.y, 0.001f);
            float3 p = input.sourcePosition * rustScale;
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

    float3 normal = ApplyNormalMap(normalMap, samp0, enableNormalMap,
                                   normalStrength, input.worldNormal,
                                   input.worldTangent, uv);
    float3 viewDir = PbrNormalizeOrDefault(
        cameraPos.xyz - input.worldPos, float3(0.0f, 0.0f, 1.0f));
    float3 shadowNormal = PbrNormalizeOrDefault(input.worldNormal,
                                                float3(0.0f, 1.0f, 0.0f));
    if (cullMode == 0 && dot(normal, viewDir) < 0.0f)
    {
        normal = -normal;
    }
    if (cullMode == 0 && dot(shadowNormal, viewDir) < 0.0f)
    {
        shadowNormal = -shadowNormal;
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
        float materialSpotShadowStrength =
            blendMode == 1 ? saturate(spotShadowParams.z) * 0.45f
                           : saturate(spotShadowParams.z);
        spotAccum *= SampleSpotShadowVisibility(
            gSpotLightShadowMap, shadowSamp, input.worldPos, shadowNormal,
            spotLightViewProjection, spotShadowParams, spotShadowFilterParams,
            materialSpotShadowStrength, blendMode == 1 ? 1.20f : 1.0f);
    }
    directLighting += pointAccum + spotAccum;

    float directShadowVisibility = 1.0f;
    if (shadowParams.x > 0.5f)
    {
        float materialShadowStrength =
            blendMode == 1 ? saturate(shadowParams.z) * 0.45f
                           : saturate(shadowParams.z);
        const float shadowSoftness = blendMode == 1 ? 0.9f : 0.45f;
        const float shadowFilterScale =
            lerp(0.85f, 1.25f, saturate(shadowSoftness));
        directShadowVisibility = SampleDirectShadowVisibility(
            gShadowMap, shadowSamp, input.worldPos, shadowNormal, lightViewProjection,
            shadowParams, shadowFilterParams, materialShadowStrength,
            shadowFilterScale);
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

    finalColor.rgb = ApplyAerialPerspectiveFog(
        finalColor.rgb, input.worldPos, cameraPos.xyz, -keyLightDirection.xyz,
        keyLightColor.rgb, ambientColor.rgb, fogColor, fogParams);

    return finalColor;
}

#ifndef PBR_LIGHTING_HLSLI
#define PBR_LIGHTING_HLSLI

static const float kPbrPi = 3.14159265f;

struct PbrMaterialValues
{
    float roughness;
    float metallic;
    float ambientOcclusion;
};

float3 PbrNormalizeOrDefault(float3 value, float3 fallback)
{
    float lenSq = dot(value, value);
    return lenSq > 0.000001f ? value * rsqrt(lenSq) : fallback;
}

float PbrWrappedDiffuse(float3 normal, float3 lightDir, float wrap)
{
    return saturate((saturate(dot(normal, lightDir)) + wrap) /
                    (1.0f + wrap));
}

float3 PbrFresnelSchlick(float cosTheta, float3 f0)
{
    float factor = pow(1.0f - saturate(cosTheta), 5.0f);
    return f0 + (1.0f - f0) * factor;
}

float3 PbrAverageFresnelSchlick(float3 f0)
{
    return f0 + (1.0f - f0) * (1.0f / 21.0f);
}

float PbrAverageSingleScatterEnergy(float roughnessValue)
{
    float smoothness = saturate(1.0f - roughnessValue);
    float energy = -0.0761947f - 0.383026f * smoothness;
    energy = 1.04997f + smoothness * energy;
    energy = 0.409255f + smoothness * energy;
    return clamp(energy, 0.0f, 0.999f);
}

float3 PbrMultiScatterSpecularEnergy(float3 f0, float roughnessValue)
{
    float missingEnergy = 1.0f - PbrAverageSingleScatterEnergy(roughnessValue);
    float3 averageFresnel = PbrAverageFresnelSchlick(f0);
    float3 denominator = max(1.0f - averageFresnel * missingEnergy, 0.001f);
    return saturate(averageFresnel * missingEnergy / denominator);
}

float PbrAreaBroadenedRoughness(float roughnessValue, float lightAngularRadius)
{
    float radius = max(lightAngularRadius, 0.0f);
    float alpha = max(roughnessValue * roughnessValue, 0.001f);
    float broadenedAlpha = saturate(alpha + radius * radius);
    return clamp(sqrt(broadenedAlpha), 0.045f, 1.0f);
}

float PbrDistributionGGX(float nDotH, float roughnessValue)
{
    float alpha = max(roughnessValue * roughnessValue, 0.001f);
    float alpha2 = alpha * alpha;
    float denom = nDotH * nDotH * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / max(kPbrPi * denom * denom, 0.0001f);
}

float PbrGeometrySchlickGGX(float nDot, float roughnessValue)
{
    float r = roughnessValue + 1.0f;
    float k = (r * r) * 0.125f;
    return nDot / max(nDot * (1.0f - k) + k, 0.0001f);
}

float PbrGeometrySmith(float nDotV, float nDotL, float roughnessValue)
{
    return PbrGeometrySchlickGGX(nDotV, roughnessValue) *
           PbrGeometrySchlickGGX(nDotL, roughnessValue);
}

PbrMaterialValues PbrSampleMaterial(Texture2D roughnessTexture,
                                    Texture2D metallicTexture,
                                    SamplerState materialSampler, float2 uv,
                                    float baseRoughness, float baseMetallic,
                                    float4 textureParams)
{
    float4 roughnessSample = roughnessTexture.Sample(materialSampler, uv);
    float4 metallicSample = metallicTexture.Sample(materialSampler, uv);
    float useRoughnessMap = textureParams.x > 0.5f ? 1.0f : 0.0f;
    float useMetallicMap = textureParams.y > 0.5f ? 1.0f : 0.0f;
    float usePackedOrm = textureParams.z > 0.5f ? 1.0f : 0.0f;
    float usePackedMetallicRoughness = textureParams.w > 0.5f ? 1.0f : 0.0f;
    float usePackedPbr = max(usePackedOrm, usePackedMetallicRoughness);
    float sampledRoughness =
        lerp(roughnessSample.r, roughnessSample.g, usePackedPbr);
    float sampledMetallic =
        lerp(metallicSample.r, roughnessSample.b, usePackedPbr);

    PbrMaterialValues values;
    values.ambientOcclusion = lerp(1.0f, roughnessSample.r, usePackedOrm);
    values.roughness = clamp(baseRoughness *
                                 lerp(1.0f, sampledRoughness, useRoughnessMap),
                             0.045f, 1.0f);
    values.metallic =
        saturate(baseMetallic * lerp(1.0f, sampledMetallic, useMetallicMap));
    return values;
}

float3 PbrEvaluateDirectArea(float3 albedo, float3 normal, float3 viewDir,
                             float3 lightDir, float3 radiance,
                             float roughnessValue, float metallicValue,
                             float diffuseWrap, float lightAngularRadius)
{
    float areaRadius = max(lightAngularRadius, 0.0f);
    float nDotLRaw = dot(normal, lightDir);
    float nDotL = saturate(nDotLRaw);
    float nDotV = max(saturate(dot(normal, viewDir)), 0.001f);
    float3 halfDir = PbrNormalizeOrDefault(lightDir + viewDir, normal);
    float nDotH = saturate(dot(normal, halfDir));
    float vDotH = saturate(dot(viewDir, halfDir));
    float areaRoughness = PbrAreaBroadenedRoughness(roughnessValue,
                                                    areaRadius);

    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallicValue);
    float3 fresnel = PbrFresnelSchlick(vDotH, f0);
    float distribution = PbrDistributionGGX(nDotH, areaRoughness);
    float geometry = PbrGeometrySmith(nDotV, nDotL, areaRoughness);
    float3 specular =
        distribution * geometry * fresnel /
        max(4.0f * nDotV * max(nDotL, 0.001f), 0.0001f);

    float3 diffuseEnergy = (1.0f - fresnel) * (1.0f - metallicValue);
    float wrappedDiffuse =
        lerp(nDotL, PbrWrappedDiffuse(normal, lightDir, diffuseWrap),
             saturate(diffuseWrap));
    float areaDiffuse =
        saturate((nDotLRaw + areaRadius * 1.65f) /
                 max(1.0f + areaRadius * 1.65f, 0.0001f));
    wrappedDiffuse =
        lerp(wrappedDiffuse, max(wrappedDiffuse, areaDiffuse),
             saturate(areaRadius * 2.5f));
    float3 diffuse = diffuseEnergy * albedo * (wrappedDiffuse / kPbrPi);
    float roughScatter = areaRoughness * areaRoughness;
    float3 multiScatterSpecular =
        PbrMultiScatterSpecularEnergy(f0, areaRoughness) *
        (wrappedDiffuse / kPbrPi) * lerp(0.35f, 1.0f, roughScatter);
    return (diffuse + specular * nDotL + multiScatterSpecular) * radiance;
}

float3 PbrEvaluateDirect(float3 albedo, float3 normal, float3 viewDir,
                         float3 lightDir, float3 radiance,
                         float roughnessValue, float metallicValue,
                         float diffuseWrap)
{
    return PbrEvaluateDirectArea(albedo, normal, viewDir, lightDir, radiance,
                                 roughnessValue, metallicValue, diffuseWrap,
                                 0.0f);
}

float3 PbrAccumulatePointLight(PointLight light, float3 albedo,
                               float3 worldPos, float3 normal, float3 viewDir,
                               float roughnessValue, float metallicValue,
                               float diffuseWrap)
{
    float3 lightVector = light.positionRange.xyz - worldPos;
    float distanceToLight = length(lightVector);
    if (distanceToLight <= 0.0001f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 lightDir = lightVector / distanceToLight;
    float attenuation =
        saturate(1.0f - distanceToLight / max(light.positionRange.w, 0.001f));
    attenuation *= attenuation;
    return PbrEvaluateDirect(albedo, normal, viewDir, lightDir,
                             light.colorIntensity.rgb * attenuation *
                                 light.colorIntensity.w,
                             roughnessValue, metallicValue, diffuseWrap);
}

float3 PbrAccumulatePointLights(PointLight point0, PointLight point1,
                                float3 albedo, float3 worldPos,
                                float3 normal, float3 viewDir,
                                float roughnessValue, float metallicValue,
                                float diffuseWrap)
{
    return PbrAccumulatePointLight(point0, albedo, worldPos, normal, viewDir,
                                   roughnessValue, metallicValue,
                                   diffuseWrap) +
           PbrAccumulatePointLight(point1, albedo, worldPos, normal, viewDir,
                                   roughnessValue, metallicValue,
                                   diffuseWrap);
}

float3 PbrAccumulateSpotLight(SpotLight spotLight, float3 albedo,
                              float3 worldPos, float3 normal, float3 viewDir,
                              float roughnessValue, float metallicValue,
                              float diffuseWrap)
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
    float coneCos = dot(-toLightDir, normalize(spotLight.direction.xyz));
    float cone = saturate((coneCos - spotLight.angleParams.y) /
                          max(spotLight.angleParams.x - spotLight.angleParams.y,
                              0.0001f));
    cone = pow(cone, max(spotLight.angleParams.z, 0.0001f));
    float attenuation =
        saturate(1.0f - spotDistance / max(spotLight.positionRange.w, 0.001f));
    attenuation *= attenuation;
    return PbrEvaluateDirect(albedo, normal, viewDir, toLightDir,
                             spotLight.colorIntensity.rgb * cone *
                                 attenuation * spotLight.colorIntensity.w,
                             roughnessValue, metallicValue, diffuseWrap);
}

float3 PbrSampleEnvironment(TextureCube<float4> environmentTexture,
                            SamplerState environmentSampler,
                            float3 reflectedVector, float roughnessValue)
{
    uint envWidth = 0;
    uint envHeight = 0;
    uint envMipLevels = 1;
    environmentTexture.GetDimensions(0, envWidth, envHeight, envMipLevels);
    float maxMipLevel = max((float)envMipLevels - 1.0f, 0.0f);
    float mipLevel = saturate(roughnessValue) * maxMipLevel;
    return environmentTexture.SampleLevel(environmentSampler, reflectedVector,
                                          mipLevel)
        .rgb;
}

float3 PbrEvaluateAmbientDiffuse(float3 albedo, float3 ambientColor,
                                 float materialAo)
{
    return ambientColor * albedo * lerp(0.38f, 1.0f, saturate(materialAo));
}

float3 PbrEvaluateAmbientSpecular(float3 albedo, float3 normal, float3 viewDir,
                                  float3 environmentRadiance,
                                  float roughnessValue, float metallicValue,
                                  float materialAo, float reflectionScale)
{
    float nDotV = saturate(dot(normal, viewDir));
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallicValue);
    float3 fresnel = PbrFresnelSchlick(nDotV, f0);
    float smoothness = saturate(1.0f - roughnessValue);
    float roughScatter = roughnessValue * roughnessValue;
    float3 multiScatterSpecular =
        PbrMultiScatterSpecularEnergy(f0, roughnessValue) *
        roughScatter * 0.42f;
    float horizon = smoothstep(0.03f, 0.42f, nDotV);
    return environmentRadiance *
           (fresnel * smoothness * smoothness + multiScatterSpecular) *
           saturate(materialAo) * horizon * reflectionScale;
}

#endif // PBR_LIGHTING_HLSLI

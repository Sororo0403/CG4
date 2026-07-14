#include "GPUParticle.hlsli"

Texture2D particleTexture : register(t1);
SamplerState particleSampler : register(s0);

float ParticleMask(float2 p, float ageRate, float randomValue)
{
    float radius = length(p);
    float angle = atan2(p.y, p.x);
    float edgeNoise = sin(angle * 8.0f + randomValue * 17.0f) * 0.035f +
                      sin(angle * 15.0f - ageRate * 4.0f) * 0.018f;
    float body = 1.0f - smoothstep(0.44f + edgeNoise, 0.62f + edgeNoise, radius);
    float core = 1.0f - smoothstep(0.02f, 0.26f, radius);
    float cross = max(1.0f - smoothstep(0.012f, 0.105f, abs(p.x)),
                      1.0f - smoothstep(0.012f, 0.105f, abs(p.y)));
    cross *= 1.0f - smoothstep(0.18f, 0.86f, radius);
    float tailFade = saturate(1.0f - ageRate * 0.65f);
    return saturate(max(body, max(core, cross * 0.32f)) * tailFade);
}

float4 main(ParticleVSOutput input) : SV_TARGET
{
    float2 p = input.localUv * 2.0f - 1.0f;
    float ageRate = input.params.x;
    float randomValue = input.params.y;

    float mask = ParticleMask(p, ageRate, randomValue);
    float4 texel = particleTexture.Sample(particleSampler, input.uv);
    float textureLuma = dot(texel.rgb, float3(0.299f, 0.587f, 0.114f));

    float3 base = max(input.color.rgb, 0.0f);
    float3 hotCore = base * (1.22f + (1.0f - ageRate) * 0.48f);
    float radialHotspot = smoothstep(0.82f, 0.06f, length(p));
    float3 rgb = lerp(base, hotCore, radialHotspot * 0.55f);
    rgb *= lerp(0.72f, 1.14f, textureLuma);

    float normalZ = sqrt(saturate(1.0f - dot(p, p)));
    float3 localNormal = normalize(float3(p.x, -p.y, normalZ));
    float3 cameraForward = normalize(cross(cameraRight.xyz, cameraUp.xyz));
    float3 worldNormal = normalize(cameraRight.xyz * localNormal.x +
                                   cameraUp.xyz * localNormal.y +
                                   cameraForward * localNormal.z);
    float3 lightDirection = normalize(lightDirectionIntensity.xyz);
    float diffuse = saturate(dot(worldNormal, -lightDirection));
    float3 lighting = ambientColor.rgb +
                      lightColor.rgb * diffuse * lightDirectionIntensity.w;
    if (input.assignedLight < min(lightConfig.x, 4u))
    {
        float4 positionRange = pointLightPositionRange[input.assignedLight];
        float4 colorIntensity = pointLightColorIntensity[input.assignedLight];
        float3 toLight = positionRange.xyz - input.worldPosition;
        float distanceToLight = length(toLight);
        float attenuation = saturate(1.0f - distanceToLight / max(0.001f, positionRange.w));
        attenuation *= attenuation;
        float3 pointDirection = distanceToLight > 0.0001f
                                    ? toLight / distanceToLight
                                    : float3(0.0f, 1.0f, 0.0f);
        float pointDiffuse = saturate(dot(worldNormal, pointDirection));
        lighting += colorIntensity.rgb * colorIntensity.w * pointDiffuse * attenuation;
    }
    rgb *= lerp(float3(1.0f, 1.0f, 1.0f), lighting,
                saturate(input.lightInfluence));

    float alpha = mask * input.color.a * lerp(0.35f, 1.0f, texel.a);
    return float4(rgb, alpha);
}

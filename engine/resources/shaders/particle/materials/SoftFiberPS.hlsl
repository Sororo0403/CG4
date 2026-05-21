#include "../GPUParticle.hlsli"

cbuffer ParticleDrawParams : register(b0)
{
    float4x4 viewProjection;
    float4 cameraRight;
    float4 cameraUp;
    float4 tintColor;
    float4 atlasInfo;
    float4 materialParams0;
    float4 materialParams1;
};

Texture2D particleTexture : register(t1);
Texture2D particleNoiseTexture : register(t2);
SamplerState particleSampler : register(s0);

float SoftFiberMask(float2 p, float edgeSoftness, float fiberNoise)
{
    float radius = length(p);
    float edge = lerp(0.42f, 0.58f, saturate(edgeSoftness));
    float fuzz = (fiberNoise - 0.5f) * 0.14f;
    return 1.0f - smoothstep(edge + fuzz, edge + 0.18f + edgeSoftness * 0.18f + fuzz, radius);
}

float4 main(ParticleVSOutput input) : SV_TARGET
{
    float2 p = input.localUv * 2.0f - 1.0f;
    float ageRate = input.params.x;
    float randomValue = input.params.y;

    float edgeSoftness = saturate(materialParams0.x);
    float fiberStrength = saturate(materialParams0.y);
    float alphaNoise = saturate(materialParams0.z);
    float innerGlow = saturate(materialParams0.w);
    float fiberScale = max(1.0f, materialParams1.x);

    float4 texel = particleTexture.Sample(particleSampler, input.uv);
    float2 noiseUv = input.localUv * fiberScale + randomValue.xx;
    float noise = particleNoiseTexture.Sample(particleSampler, noiseUv).r;
    float threadNoise =
        sin((p.x + p.y * 0.37f + randomValue) * fiberScale * 6.0f) * 0.5f + 0.5f;
    float fiber = lerp(noise, threadNoise, 0.45f);

    float mask = SoftFiberMask(p, edgeSoftness, fiber);
    float3 base = saturate(input.color.rgb);
    float core = 1.0f - smoothstep(0.0f, 0.52f, length(p));
    float3 warmCore = base * (1.0f + innerGlow * core * 0.85f);
    float3 rgb = lerp(base, warmCore, core * 0.5f);
    rgb *= lerp(0.82f, 1.18f, fiber * fiberStrength);
    rgb *= lerp(0.88f, 1.12f, texel.a);

    float ageFade = saturate(1.0f - ageRate * 0.45f);
    float alpha = mask * input.color.a * ageFade;
    alpha *= lerp(1.0f, lerp(0.72f, 1.1f, fiber), alphaNoise);
    alpha *= lerp(0.45f, 1.0f, texel.a);

    return float4(saturate(rgb), saturate(alpha));
}

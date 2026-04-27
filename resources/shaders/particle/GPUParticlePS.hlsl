#include "GPUParticle.hlsli"

Texture2D particleTexture : register(t1);
SamplerState particleSampler : register(s0);

float4 main(ParticleVSOutput input) : SV_TARGET
{
    float4 texColor = particleTexture.Sample(particleSampler, input.uv);
    float4 color = texColor * input.color;
    color.rgb *= 1.45f;
    color.a *= texColor.r;
    return color;
}

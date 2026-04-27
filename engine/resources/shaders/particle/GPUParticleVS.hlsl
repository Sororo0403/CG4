#include "GPUParticle.hlsli"

cbuffer ParticleDrawParams : register(b0)
{
    float4x4 viewProjection;
    float4 cameraRight;
    float4 cameraUp;
    float4 tintColor;
};

StructuredBuffer<Particle> gParticles : register(t0);

static const float2 kPositions[6] =
{
    float2(-1.0f, 1.0f),
    float2(1.0f, 1.0f),
    float2(-1.0f, -1.0f),
    float2(-1.0f, -1.0f),
    float2(1.0f, 1.0f),
    float2(1.0f, -1.0f),
};

static const float2 kUvs[6] =
{
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(0.0f, 1.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, 0.0f),
    float2(1.0f, 1.0f),
};

ParticleVSOutput main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    Particle particle = gParticles[instanceId];
    float ageRate = saturate(particle.currentTime / max(particle.lifeTime, 0.001f));
    float pulse = 1.0f + sin(ageRate * 3.1415926f) * 0.16f;
    float2 local = kPositions[vertexId] * particle.scale * lerp(1.18f, 0.64f, ageRate) * pulse;
    float roll = sin(particle.seed * 0.13f + particle.currentTime * 5.6f) * 0.95f +
                 particle.currentTime * 0.55f;
    float s = sin(roll);
    float c = cos(roll);
    local = float2(local.x * c - local.y * s, local.x * s + local.y * c);

    float3 worldPosition =
        particle.translate +
        cameraRight.xyz * local.x +
        cameraUp.xyz * local.y;

    ParticleVSOutput output;
    output.position = mul(float4(worldPosition, 1.0f), viewProjection);
    output.uv = kUvs[vertexId];
    output.color = particle.color * tintColor;
    output.color.a *= smoothstep(0.0f, 0.10f, ageRate) * (1.0f - ageRate);
    output.params = float2(frac(particle.seed * 0.173f), ageRate);
    return output;
}

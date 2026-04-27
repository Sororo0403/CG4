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
    float2 local = kPositions[vertexId] * particle.scale * lerp(1.25f, 0.55f, ageRate);
    float3 worldPosition =
        particle.translate +
        cameraRight.xyz * local.x +
        cameraUp.xyz * local.y;

    ParticleVSOutput output;
    output.position = mul(float4(worldPosition, 1.0f), viewProjection);
    output.uv = kUvs[vertexId];
    output.color = particle.color * tintColor;
    output.color.a *= smoothstep(0.0f, 0.12f, ageRate) * (1.0f - ageRate);
    return output;
}

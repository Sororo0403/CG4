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
    uint quadVertexId = vertexId % 6u;
    Particle particle = gParticles[instanceId];

    float ageRate = saturate(particle.currentTime / max(particle.lifeTime, 0.001f));

    float scale = lerp(particle.params0.x, particle.params0.y, ageRate);
    float stretch = max(0.0f, particle.params1.y);
    float2 localScale = float2(scale * (1.0f + stretch), scale);
    float2 local = kPositions[quadVertexId] * localScale;

    float roll = particle.seed * 0.017f + particle.currentTime * 0.7f;
    float s = sin(roll);
    float c = cos(roll);
    local = float2(local.x * c - local.y * s, local.x * s + local.y * c);

    float3 worldPosition =
        particle.translate +
        cameraRight.xyz * local.x +
        cameraUp.xyz * local.y;

    ParticleVSOutput output;
    output.position = mul(float4(worldPosition, 1.0f), viewProjection);
    output.uv = kUvs[quadVertexId];
    output.color = particle.color * tintColor;
    output.params = float2(ageRate, particle.params1.z);
    return output;
}

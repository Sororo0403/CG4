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
    float emissionMode = particle.params.x;
    float pulse = 1.0f + sin(ageRate * 3.1415926f) * 0.16f;
    float2 modeScale = emissionMode < 0.5f
                            ? float2(lerp(1.34f, 0.18f, ageRate), lerp(0.78f, 0.14f, ageRate))
                            : emissionMode < 1.5f
                                  ? float2(lerp(0.70f, 1.48f, ageRate), lerp(0.70f, 1.48f, ageRate))
                            : emissionMode > 2.5f && emissionMode < 3.5f
                                  ? float2(lerp(1.34f, 0.82f, ageRate), lerp(1.12f, 0.54f, ageRate))
                                  : float2(lerp(0.54f, 1.72f, ageRate), lerp(0.54f, 1.72f, ageRate));
    float2 local = kPositions[vertexId] * particle.scale * modeScale * pulse;
    float roll = emissionMode > 2.5f && emissionMode < 3.5f
                     ? particle.params.y +
                           sin(particle.seed * 1.7f) * 0.055f
                     : sin(particle.seed * 0.13f + particle.currentTime * 5.6f) * 0.95f +
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
    output.params = float2(emissionMode, ageRate);
    return output;
}

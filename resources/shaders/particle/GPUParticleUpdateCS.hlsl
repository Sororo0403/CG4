#include "GPUParticle.hlsli"

cbuffer ParticleUpdateParams : register(b0)
{
    float4 emitterPositionAndTime;
    float4 params;
};

RWStructuredBuffer<Particle> gParticles : register(u0);

float Random(float2 value)
{
    return frac(sin(dot(value, float2(12.9898f, 78.233f))) * 43758.5453f);
}

void Respawn(uint index, inout Particle particle)
{
    float time = emitterPositionAndTime.w;
    float seed = particle.seed + (float) index * 17.13f + time * 3.71f;
    float r0 = Random(float2(seed, 0.11f));
    float r1 = Random(float2(seed, 1.37f));
    float r2 = Random(float2(seed, 2.71f));
    float r3 = Random(float2(seed, 4.29f));

    float angle = r0 * 6.2831853f;
    float radius = 0.05f + r1 * 0.34f;
    particle.translate = emitterPositionAndTime.xyz +
                         float3(cos(angle) * radius, 0.0f,
                                sin(angle) * radius * 0.45f);
    particle.velocity = float3(cos(angle) * (0.20f + r2 * 0.35f),
                               0.55f + r3 * 0.95f,
                               sin(angle) * (0.15f + r1 * 0.25f));
    particle.currentTime = 0.0f;
    particle.lifeTime = 1.2f + r2 * 1.5f;
    particle.color = float4(0.46f + r2 * 0.24f, 0.72f + r1 * 0.20f, 1.0f,
                            0.42f + r3 * 0.38f);
    float scale = 0.045f + r0 * 0.095f;
    particle.scale = float2(scale, scale);
    particle.seed = seed + 19.19f;
}

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    uint particleCount = (uint) params.y;
    if (index >= particleCount)
    {
        return;
    }

    float deltaTime = params.x;
    Particle particle = gParticles[index];

    particle.currentTime += deltaTime;
    if (particle.currentTime >= particle.lifeTime)
    {
        Respawn(index, particle);
    }
    else
    {
        float ageRate = saturate(particle.currentTime / max(particle.lifeTime, 0.001f));
        float wave = sin(emitterPositionAndTime.w * 2.0f + particle.seed);
        float3 wind = float3(0.10f + wave * 0.08f, 0.0f, 0.0f);
        float3 gravity = float3(0.0f, -0.08f, 0.0f);

        particle.velocity += (wind + gravity) * deltaTime;
        particle.translate += particle.velocity * deltaTime;
        particle.color.a = saturate(1.0f - ageRate) * 0.75f;
    }

    gParticles[index] = particle;
}

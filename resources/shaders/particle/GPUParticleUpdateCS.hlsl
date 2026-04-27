#include "GPUParticle.hlsli"

cbuffer ParticleUpdateParams : register(b0)
{
    float4 time;
};

cbuffer EmitterParams : register(b1)
{
    float3 emitterTranslate;
    float emitterRadius;
    uint emitterCount;
    float emitterFrequency;
    float emitterFrequencyTime;
    uint emitterEmit;
};

RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<uint> gEmitCounter : register(u1);

struct RandomGenerator
{
    float3 seed;

    float Generate1d()
    {
        seed = frac(seed * float3(12.9898f, 78.233f, 37.719f) + 0.12345f);
        return frac(sin(dot(seed.xy, float2(12.9898f, 78.233f))) * 43758.5453f);
    }

    float3 Generate3d()
    {
        return float3(Generate1d(), Generate1d(), Generate1d());
    }
};

void Respawn(uint index, inout Particle particle)
{
    RandomGenerator generator;
    generator.seed = float3(particle.seed + (float) index * 17.13f,
                            time.x * 3.71f + 0.11f,
                            emitterFrequencyTime + 2.71f);
    float r0 = generator.Generate1d();
    float r1 = generator.Generate1d();
    float r2 = generator.Generate1d();
    float r3 = generator.Generate1d();

    float angle = r0 * 6.2831853f;
    float radius = emitterRadius * sqrt(r1);
    particle.translate = emitterTranslate +
                         float3(cos(angle) * radius, (r2 - 0.5f) * emitterRadius,
                                sin(angle) * radius);
    particle.velocity = float3(cos(angle) * (0.20f + r2 * 0.35f),
                               0.55f + r3 * 0.95f,
                               sin(angle) * (0.15f + r1 * 0.25f));
    particle.currentTime = 0.0f;
    particle.lifeTime = 1.2f + r2 * 1.5f;
    particle.color = float4(0.46f + r2 * 0.24f, 0.72f + r1 * 0.20f, 1.0f,
                            0.42f + r3 * 0.38f);
    float scale = 0.045f + r0 * 0.095f;
    particle.scale = float2(scale, scale);
    particle.seed += 19.19f + time.x;
}

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    uint particleCount = (uint) time.z;
    if (index >= particleCount)
    {
        return;
    }

    float deltaTime = time.y;
    Particle particle = gParticles[index];

    if (particle.currentTime < particle.lifeTime)
    {
        particle.currentTime += deltaTime;
        float ageRate = saturate(particle.currentTime / max(particle.lifeTime, 0.001f));
        float wave = sin(time.x * 2.0f + particle.seed);
        float3 wind = float3(0.10f + wave * 0.08f, 0.0f, 0.0f);
        float3 gravity = float3(0.0f, -0.08f, 0.0f);

        particle.velocity += (wind + gravity) * deltaTime;
        particle.translate += particle.velocity * deltaTime;
        particle.color.a = saturate(1.0f - ageRate) * 0.75f;
    }

    if (particle.currentTime >= particle.lifeTime)
    {
        particle.color.a = 0.0f;

        if (emitterEmit != 0)
        {
            uint particleIndex = 0;
            InterlockedAdd(gEmitCounter[0], 1, particleIndex);
            if (particleIndex < emitterCount)
            {
                Respawn(index, particle);
            }
        }
    }

    gParticles[index] = particle;
}

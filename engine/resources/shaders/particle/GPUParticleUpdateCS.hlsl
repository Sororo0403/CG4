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
RWStructuredBuffer<uint> gFreeList : register(u1);
RWStructuredBuffer<int> gFreeListIndex : register(u2);

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
    float r4 = generator.Generate1d();
    float r5 = generator.Generate1d();

    float angle = r0 * 6.2831853f;
    float radius = emitterRadius * sqrt(r1);
    float tangent = (r5 < 0.5f) ? -1.0f : 1.0f;
    particle.translate = emitterTranslate +
                         float3(cos(angle) * radius, (r2 - 0.58f) * emitterRadius,
                                sin(angle) * radius * 0.24f);
    particle.velocity = float3(cos(angle) * (0.18f + r2 * 0.32f) +
                                   -sin(angle) * tangent * (0.18f + r1 * 0.20f),
                               0.62f + r3 * 0.78f,
                               sin(angle) * (0.08f + r1 * 0.16f) +
                                   cos(angle) * tangent * 0.10f);
    particle.currentTime = 0.0f;
    particle.lifeTime = 1.05f + r2 * 0.85f;

    float4 palette[6] =
    {
        float4(1.00f, 0.54f, 0.36f, 1.0f),
        float4(1.00f, 0.78f, 0.25f, 1.0f),
        float4(0.37f, 0.86f, 0.72f, 1.0f),
        float4(0.34f, 0.66f, 1.00f, 1.0f),
        float4(0.94f, 0.45f, 0.80f, 1.0f),
        float4(0.75f, 0.60f, 1.00f, 1.0f),
    };
    particle.color = palette[(uint) (r4 * 6.0f) % 6u];
    particle.color.a = 0.96f;

    float scale = 0.060f + r0 * 0.090f;
    particle.scale = float2(scale * (0.78f + r3 * 0.54f), scale);
    particle.seed += 19.19f + time.x;
    particle.isActive = 1;
}

bool TryPopFreeList(out uint particleIndex)
{
    particleIndex = 0;

    int freeListIndex = 0;
    InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);
    if (freeListIndex <= 0)
    {
        InterlockedAdd(gFreeListIndex[0], 1);
        return false;
    }

    particleIndex = gFreeList[freeListIndex - 1];
    return true;
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

    Particle particle = gParticles[index];

    if (particle.isActive != 0)
    {
        float deltaTime = time.y;
        particle.currentTime += deltaTime;
        float ageRate = saturate(particle.currentTime / max(particle.lifeTime, 0.001f));
        float wave = sin(time.x * 3.6f + particle.seed);
        float3 wind = float3(0.14f + wave * 0.18f,
                             sin(time.x * 2.0f + particle.seed) * 0.09f,
                             cos(time.x * 1.7f + particle.seed) * 0.04f);
        float3 gravity = float3(0.0f, -0.22f, 0.0f);

        particle.velocity += (wind + gravity) * deltaTime;
        particle.translate += particle.velocity * deltaTime;
        particle.color.a = saturate(1.0f - ageRate) * 0.96f;

        if (particle.currentTime >= particle.lifeTime)
        {
            particle.isActive = 0;
            particle.color.a = 0.0f;
            gParticles[index] = particle;

            int freeListIndex = 0;
            InterlockedAdd(gFreeListIndex[0], 1, freeListIndex);
            if (freeListIndex < (int) particleCount)
            {
                gFreeList[freeListIndex] = index;
            }
        } else
        {
            gParticles[index] = particle;
        }
    }

    if (emitterEmit != 0 && index < emitterCount)
    {
        uint particleIndex = 0;
        if (TryPopFreeList(particleIndex))
        {
            Particle respawnParticle = gParticles[particleIndex];
            Respawn(particleIndex, respawnParticle);
            gParticles[particleIndex] = respawnParticle;
        }
    }
}

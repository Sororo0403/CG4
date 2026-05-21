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
    float4 emitterTintColor;
    float4 emitterDirectionSpeed;
    uint emitterEmissionMode;
    float emitterBaseLifeTime;
    float emitterLifeTimeRandom;
    float emitterBaseScale;
    float emitterScaleRandom;
    float emitterGravity;
    float emitterTurbulence;
    float emitterAlpha;
    float emitterStretch;
    float3 emitterReserved;
};

RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<uint> gFreeList : register(u1);
RWStructuredBuffer<int> gFreeListIndex : register(u2);

#define PARTICLE_THREAD_COUNT 256

struct RandomGenerator
{
    uint state;

    void Initialize(uint index, float particleSeed)
    {
        state = asuint(particleSeed) ^ (index * 747796405u) ^
                (asuint(time.x) * 2891336453u) ^ 0x9E3779B9u;
        state = state == 0u ? 0xA341316Cu : state;
    }

    float Generate1d()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (state & 0x00FFFFFFu) / 16777216.0f;
    }

    float3 Generate3d()
    {
        return float3(Generate1d(), Generate1d(), Generate1d());
    }
};

void Respawn(uint index, inout Particle particle)
{
    RandomGenerator generator;
    generator.Initialize(index, particle.seed + emitterFrequencyTime);
    float r0 = generator.Generate1d();
    float r1 = generator.Generate1d();
    float r2 = generator.Generate1d();
    float r3 = generator.Generate1d();
    float r4 = generator.Generate1d();
    float r5 = generator.Generate1d();
    float r6 = generator.Generate1d();

    float angle = r0 * 6.2831853f;
    float radius = emitterRadius * sqrt(r1);
    float tangent = (r5 < 0.5f) ? -1.0f : 1.0f;
    float3 emitDir = normalize(emitterDirectionSpeed.xyz);
    if (length(emitterDirectionSpeed.xyz) < 0.0001f)
    {
        emitDir = float3(0.0f, 1.0f, 0.0f);
    }
    float3 radial = normalize(float3(cos(angle), r2 * 0.65f + 0.12f, sin(angle)));

    particle.translate = emitterTranslate +
                         float3(cos(angle) * radius, (r2 - 0.58f) * emitterRadius,
                                sin(angle) * radius * 0.24f);
    if (emitterEmissionMode == 0u)
    {
        float3 side = normalize(float3(-emitDir.z, 0.0f, emitDir.x) +
                                radial * (r3 - 0.5f) * 0.65f);
        particle.velocity = emitDir * (1.10f + r0 * 1.90f) * emitterDirectionSpeed.w +
                            side * (1.20f + r1 * 1.80f) +
                            float3(0.0f, 0.76f + r2 * 0.72f, 0.0f);
    } else if (emitterEmissionMode == 1u)
    {
        particle.velocity = radial * (1.15f + r0 * 1.95f) * emitterDirectionSpeed.w +
                            emitDir * (0.24f + r6 * 0.50f) +
                            float3(0.0f, 0.48f + r3 * 0.85f, 0.0f);
    } else if (emitterEmissionMode == 3u)
    {
        particle.translate = emitterTranslate +
                             float3((r0 - 0.5f) * emitterRadius * 0.20f,
                                    (r1 - 0.5f) * emitterRadius * 0.20f,
                                    (r2 - 0.5f) * emitterRadius * 0.08f);
        particle.velocity = radial * (0.16f + r0 * 0.42f) *
                            emitterDirectionSpeed.w;
    } else
    {
        float3 buoyantDir = normalize(radial * float3(1.0f, 0.45f, 1.0f) +
                                      float3(0.0f, 0.65f + r3 * 0.55f, 0.0f));
        particle.velocity = buoyantDir * (0.42f + r0 * 0.80f) * emitterDirectionSpeed.w +
                            emitDir * (0.08f + r6 * 0.18f);
    }
    particle.currentTime = 0.0f;
    particle.lifeTime = emitterBaseLifeTime + r2 * emitterLifeTimeRandom;

    float brightness = 0.78f + r4 * 0.34f;
    if (emitterEmissionMode == 2u)
    {
        brightness = 0.46f + r4 * 0.28f;
    } else if (emitterEmissionMode == 3u)
    {
        brightness = 0.92f + r4 * 0.34f;
    }
    particle.color = float4(saturate(emitterTintColor.rgb * brightness),
                            emitterAlpha);

    float scale = emitterBaseScale + r0 * emitterScaleRandom;
    particle.scale = emitterEmissionMode == 0u
                         ? float2(scale * (emitterStretch + r3 * emitterStretch * 0.48f),
                                  scale * 0.26f)
                         : float2(scale * (0.90f + r3 * 0.36f), scale);
    particle.seed += 19.19f + time.x;
    particle.params.x = (float) emitterEmissionMode;
    particle.params.y = r4;
    particle.params.z = emitterAlpha;
    particle.isActive = 1;
}

[numthreads(PARTICLE_THREAD_COUNT, 1, 1)]
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
        float3 wander = float3(0.14f + wave * 0.18f,
                               sin(time.x * 2.0f + particle.seed) * 0.09f,
                               cos(time.x * 1.7f + particle.seed) * 0.04f) *
                        emitterTurbulence;
        float3 gravity = float3(0.0f, emitterGravity, 0.0f);
        if (particle.params.x > 1.5f)
        {
            wander = float3(0.08f + wave * 0.12f,
                            0.18f + sin(time.x * 1.3f + particle.seed) * 0.05f,
                            cos(time.x * 1.1f + particle.seed) * 0.08f) *
                    emitterTurbulence;
        }

        particle.velocity += (wander + gravity) * deltaTime;
        particle.translate += particle.velocity * deltaTime;
        particle.color.a = particle.params.x < 0.5f
                               ? saturate(1.0f - ageRate) * particle.params.z
                               : particle.params.x > 2.5f &&
                                         particle.params.x < 3.5f
                                     ? smoothstep(0.0f, 0.08f, ageRate) *
                                           saturate(1.0f - ageRate) *
                                           particle.params.z
                               : particle.params.x > 1.5f
                                     ? smoothstep(0.0f, 0.18f, ageRate) *
                                           saturate(1.0f - ageRate) *
                                           particle.params.z
                                     : saturate(1.0f - ageRate) *
                                           particle.params.z;

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
            } else
            {
                InterlockedAdd(gFreeListIndex[0], -1);
            }
        } else
        {
            gParticles[index] = particle;
        }
    }

    if (emitterEmit != 0 && index < emitterCount)
    {
        int freeListIndex = 0;
        InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);
        if (freeListIndex <= 0)
        {
            InterlockedAdd(gFreeListIndex[0], 1);
        } else
        {
            uint particleIndex = gFreeList[freeListIndex - 1];
            Particle respawnParticle = gParticles[particleIndex];
            Respawn(particleIndex, respawnParticle);
            gParticles[particleIndex] = respawnParticle;
        }
    }
}

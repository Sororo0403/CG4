#include "GPUParticle.hlsli"

cbuffer ParticleUpdateParams : register(b0)
{
    float4 time;
};

cbuffer EmitterParams : register(b1)
{
    float4 emitterPosition;
    float4 emitterSpawnOffsetScale;
    float4 emitterDirectionAndDirectionalVelocity;
    float4 emitterVelocityBiasAndRadialVelocity;
    float4 emitterLifeAndFade;
    float4 emitterScale;
    float4 emitterAccelerationAndTurbulence;
    float4 emitterMotion;
    float4 emitterTintColor;
    uint4 emitterConfig;
};

RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<uint> gFreeList : register(u1);
RWStructuredBuffer<int> gFreeListIndex : register(u2);

#define PARTICLE_THREAD_COUNT 256
#define SPAWN_SHAPE_POINT 0u
#define SPAWN_SHAPE_SPHERE 1u
#define SPAWN_SHAPE_BOX 2u
#define SPAWN_SHAPE_RING 3u
#define SPAWN_SHAPE_DISK 4u

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
};

float3 SafeNormalize(float3 value, float3 fallback)
{
    float len = length(value);
    return len < 0.0001f ? fallback : value / len;
}

float3 MakeSphereDirection(float u0, float u1)
{
    float z = u0 * 2.0f - 1.0f;
    float angle = u1 * 6.2831853f;
    float radius = sqrt(max(0.0f, 1.0f - z * z));
    return float3(cos(angle) * radius, z, sin(angle) * radius);
}

float3 MakeSpawnOffset(uint spawnShape, float r0, float r1, float r2)
{
    if (spawnShape == SPAWN_SHAPE_POINT)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    if (spawnShape == SPAWN_SHAPE_BOX)
    {
        return float3(r0 * 2.0f - 1.0f, r1 * 2.0f - 1.0f,
                      r2 * 2.0f - 1.0f);
    }

    float angle = r0 * 6.2831853f;
    if (spawnShape == SPAWN_SHAPE_RING)
    {
        return float3(cos(angle), 0.0f, sin(angle));
    }

    if (spawnShape == SPAWN_SHAPE_DISK)
    {
        float radius = sqrt(r1);
        return float3(cos(angle) * radius, 0.0f, sin(angle) * radius);
    }

    float radius3d = pow(max(r2, 0.0001f), 0.3333333f);
    return MakeSphereDirection(r0, r1) * radius3d;
}

float3 MakeTurbulence(float seed, float age)
{
    return float3(sin(age * 11.7f + seed * 0.31f),
                  cos(age * 9.1f + seed * 0.43f),
                  sin(age * 7.4f + seed * 0.59f));
}

void Respawn(uint index, inout Particle particle)
{
    RandomGenerator generator;
    generator.Initialize(index, particle.seed + time.x);
    float r0 = generator.Generate1d();
    float r1 = generator.Generate1d();
    float r2 = generator.Generate1d();
    float r3 = generator.Generate1d();
    float r4 = generator.Generate1d();
    float r5 = generator.Generate1d();
    float r6 = generator.Generate1d();

    uint spawnShape = emitterConfig.y;
    float3 offset = MakeSpawnOffset(spawnShape, r0, r1, r2);
    float3 fallbackDirection = MakeSphereDirection(r3, r4);
    float3 radialDirection = SafeNormalize(offset, fallbackDirection);
    if (spawnShape == SPAWN_SHAPE_RING || spawnShape == SPAWN_SHAPE_DISK)
    {
        radialDirection = SafeNormalize(float3(offset.x, 0.0f, offset.z),
                                        float3(1.0f, 0.0f, 0.0f));
    }

    float3 direction =
        SafeNormalize(emitterDirectionAndDirectionalVelocity.xyz,
                      float3(0.0f, 1.0f, 0.0f));
    float directionalVelocity = emitterDirectionAndDirectionalVelocity.w;
    float3 velocityBias = emitterVelocityBiasAndRadialVelocity.xyz;
    float radialVelocity = emitterVelocityBiasAndRadialVelocity.w;

    particle.translate =
        emitterPosition.xyz + offset * emitterSpawnOffsetScale.xyz;
    particle.velocity = radialDirection * radialVelocity +
                        direction * directionalVelocity + velocityBias;
    particle.currentTime = 0.0f;
    particle.lifeTime =
        max(0.01f, emitterLifeAndFade.x + r5 * emitterLifeAndFade.y);

    float startScale = max(0.0f, emitterScale.x + r6 * emitterScale.z);
    float endScale = max(0.0f, emitterScale.y);
    particle.scale = float2(startScale, startScale);
    particle.color = emitterTintColor;
    particle.seed += 19.19f + time.x + r4;
    particle.params0 =
        float4(startScale, endScale, emitterLifeAndFade.z, emitterLifeAndFade.w);
    particle.params1 = float4(max(0.01f, emitterMotion.y),
                              max(0.0f, emitterScale.w), r4, 0.0f);
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
            float turbulence = emitterAccelerationAndTurbulence.w;
            float3 wander =
                MakeTurbulence(particle.seed, particle.currentTime) * turbulence;
            float damping = pow(max(emitterMotion.x, 0.0f), deltaTime * 60.0f);
            particle.velocity +=
                (emitterAccelerationAndTurbulence.xyz + wander) * deltaTime;
            particle.velocity *= damping;
            particle.translate += particle.velocity * deltaTime;

            float alpha = emitterTintColor.a;
            float fadeInTime = particle.params0.z;
            if (fadeInTime > 0.0f)
            {
                alpha *= saturate(particle.currentTime / fadeInTime);
            }

            float fadeOutTime = particle.params0.w;
            if (fadeOutTime > 0.0f)
            {
                float remaining = particle.lifeTime - particle.currentTime;
                float fade = saturate(remaining / fadeOutTime);
                alpha *= pow(fade, particle.params1.x);
            }
            particle.color = emitterTintColor;
            particle.color.a = alpha;
            gParticles[index] = particle;
        }
    }

    if (emitterConfig.w != 0u && index < emitterConfig.z)
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

#include "GPUParticle.hlsli"

cbuffer ParticleUpdateParams : register(b0)
{
    float4 time;
};

cbuffer EmitterParams : register(b1)
{
    float4 emitterPosition;
    float4 emitterSpawnOffsetScale;
    float4 emitterBasisRight;
    float4 emitterBasisUp;
    float4 emitterBasisForward;
    float4 emitterDirectionAndDirectionalVelocity;
    float4 emitterVelocityBiasAndRadialVelocity;
    float4 emitterLifeAndFade;
    float4 emitterScale;
    float4 emitterAccelerationAndTurbulence;
    float4 emitterMotion;
    float4 emitterAtlasAndRotation;
    float4 emitterTintColor;
    uint3 emitterConfig;
};

RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<uint> gFreeList : register(u1);
RWStructuredBuffer<int> gFreeListIndex : register(u2);
RWStructuredBuffer<uint> gActiveIndices : register(u3);
RWByteAddressBuffer gActiveCount : register(u4);
RWByteAddressBuffer gDrawArgsBuffer : register(u5);

struct ExplicitParticleSpawn
{
    float4 positionLife;
    float4 velocityStartScale;
    float4 color;
    float4 scaleFade;
    float4 motion;
    float4 accelerationAtlas;
    float4 drawAxis;
    uint4 atlas;
};

StructuredBuffer<ExplicitParticleSpawn> gExplicitSpawns : register(t0);

#define PARTICLE_THREAD_COUNT 256
#define SPAWN_SHAPE_POINT 0u
#define SPAWN_SHAPE_SPHERE 1u
#define SPAWN_SHAPE_BOX 2u
#define SPAWN_SHAPE_RING 3u
#define SPAWN_SHAPE_DISK 4u
#define SPAWN_SHAPE_ARC 5u

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

float3 MakeSpawnOffset(uint spawnShape, float r0, float r1, float r2,
                       float4 shapeParams)
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

    if (spawnShape == SPAWN_SHAPE_ARC)
    {
        float arcAngle = max(0.01f, shapeParams.x);
        float halfAngle = arcAngle * 0.5f;
        float arc = lerp(-halfAngle, halfAngle, r0);
        float thickness = r1 * 2.0f - 1.0f;
        float depth = r2 * 2.0f - 1.0f;
        return float3(sin(arc), cos(arc) - cos(halfAngle), depth * 0.10f) +
               float3(0.0f, thickness * 0.08f, 0.0f);
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
    float3 offset =
        MakeSpawnOffset(spawnShape, r0, r1, r2,
                        float4(emitterSpawnOffsetScale.w, 0.0f, 0.0f, 0.0f));
    float3 scaledOffset = offset * emitterSpawnOffsetScale.xyz;
    float3 worldOffset = emitterBasisRight.xyz * scaledOffset.x +
                         emitterBasisUp.xyz * scaledOffset.y +
                         emitterBasisForward.xyz * scaledOffset.z;
    float3 fallbackDirection = MakeSphereDirection(r3, r4);
    float3 radialDirection = SafeNormalize(worldOffset, fallbackDirection);
    if (spawnShape == SPAWN_SHAPE_RING || spawnShape == SPAWN_SHAPE_DISK)
    {
        float3 planeOffset = emitterBasisRight.xyz * scaledOffset.x +
                             emitterBasisUp.xyz * scaledOffset.y;
        radialDirection = SafeNormalize(planeOffset, emitterBasisRight.xyz);
    }
    if (spawnShape == SPAWN_SHAPE_ARC)
    {
        radialDirection =
            SafeNormalize(emitterBasisUp.xyz * 0.35f + emitterBasisForward.xyz,
                          emitterBasisForward.xyz);
    }

    float3 direction =
        SafeNormalize(emitterDirectionAndDirectionalVelocity.xyz,
                      float3(0.0f, 1.0f, 0.0f));
    float directionalVelocity = emitterDirectionAndDirectionalVelocity.w;
    float3 velocityBias = emitterVelocityBiasAndRadialVelocity.xyz;
    float radialVelocity = emitterVelocityBiasAndRadialVelocity.w;

    particle.translate =
        emitterPosition.xyz + worldOffset;
    particle.velocity = radialDirection * radialVelocity +
                        direction * directionalVelocity + velocityBias;
    particle.currentTime = 0.0f;
    particle.lifeTime =
        max(0.01f, emitterLifeAndFade.x + r5 * emitterLifeAndFade.y);

    float startScale = max(0.0f, emitterScale.x + r6 * emitterScale.z);
    float endScale = max(0.0f, emitterScale.y);
    float atlasFrameCount = max(1.0f, emitterAtlasAndRotation.y);
    float frameIndex =
        emitterAtlasAndRotation.x + floor(r3 * atlasFrameCount);
    particle.scale = float2(frameIndex, emitterAtlasAndRotation.z);
    particle.color = emitterTintColor;
    particle.seed += 19.19f + time.x + r4;
    particle.params0 =
        float4(startScale, endScale, emitterLifeAndFade.z, emitterLifeAndFade.w);
    float initialRoll = emitterAtlasAndRotation.w > 0.5f
                            ? 6.2831853f * r4
                            : 0.0f;
    particle.params1 = float4(max(0.01f, emitterMotion.y),
                              max(0.0f, emitterScale.w), r4, initialRoll);
    particle.params2 = float4(emitterAccelerationAndTurbulence.w,
                              max(0.0f, emitterMotion.x), emitterTintColor.a,
                              max(1.0f, emitterMotion.z));
    particle.params3 =
        float4(emitterAccelerationAndTurbulence.xyz, max(1.0f, emitterMotion.w));
    particle.params4 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    particle.isActive = 1;
}

void RespawnExplicit(uint particleIndex, uint spawnIndex, inout Particle particle)
{
    ExplicitParticleSpawn spawn = gExplicitSpawns[spawnIndex];

    RandomGenerator generator;
    generator.Initialize(particleIndex + spawnIndex * 4099u, particle.seed + time.x);
    float random = generator.Generate1d();

    particle.translate = spawn.positionLife.xyz;
    particle.velocity = spawn.velocityStartScale.xyz;
    particle.currentTime = 0.0f;
    particle.lifeTime = max(0.01f, spawn.positionLife.w);
    particle.color = spawn.color;
    particle.scale = float2(spawn.accelerationAtlas.w, spawn.motion.w);
    particle.seed += 23.17f + time.x + random;
    particle.params0 =
        float4(max(0.0f, spawn.velocityStartScale.w),
               max(0.0f, spawn.scaleFade.x),
               max(0.0f, spawn.scaleFade.y),
               max(0.0f, spawn.scaleFade.z));
    float initialRoll = spawn.atlas.z != 0u ? 6.2831853f * random : 0.0f;
    particle.params1 =
        float4(max(0.01f, spawn.scaleFade.w), max(0.0f, spawn.motion.x),
               random, initialRoll);
    particle.params2 =
        float4(spawn.motion.y, max(0.0f, spawn.motion.z), spawn.color.a,
               max(1.0f, (float) spawn.atlas.x));
    particle.params3 =
        float4(spawn.accelerationAtlas.xyz, max(1.0f, (float) spawn.atlas.y));
    particle.params4 = spawn.drawAxis;
    particle.isActive = 1;
}

void AppendActiveParticle(uint index, uint particleCount, Particle particle)
{
    if (particle.isActive == 0u || particle.color.a <= 0.0001f)
    {
        return;
    }

    uint activeIndex = 0u;
    gActiveCount.InterlockedAdd(0, 1u, activeIndex);
    if (activeIndex < particleCount)
    {
        gActiveIndices[activeIndex] = index;
        gDrawArgsBuffer.InterlockedMax(4, activeIndex + 1u);
    }
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

    uint emitMode = (uint) round(emitterPosition.w);

    if (emitMode == 2u)
    {
        if (index < emitterConfig.z)
        {
            int freeListIndex = 0;
            InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);
            if (freeListIndex <= 0)
            {
                InterlockedAdd(gFreeListIndex[0], 1);
            } else if (freeListIndex > (int) particleCount)
            {
                InterlockedAdd(gFreeListIndex[0], 1);
            } else
            {
                uint particleIndex = gFreeList[freeListIndex - 1];
                if (particleIndex >= particleCount)
                {
                    InterlockedAdd(gFreeListIndex[0], 1);
                    return;
                }
                Particle respawnParticle = gParticles[particleIndex];
                RespawnExplicit(particleIndex, index, respawnParticle);
                gParticles[particleIndex] = respawnParticle;
                AppendActiveParticle(particleIndex, particleCount, respawnParticle);
            }
        }
        return;
    }

    if (emitMode == 0u)
    {
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
                float deltaTime = time.y;
                float turbulence = particle.params2.x;
                float3 wander =
                    MakeTurbulence(particle.seed, particle.currentTime) * turbulence;
                float damping = pow(max(particle.params2.y, 0.0f), deltaTime * 60.0f);
                particle.velocity +=
                    (particle.params3.xyz + wander) * deltaTime;
                particle.velocity *= damping;
                particle.translate += particle.velocity * deltaTime;

                float alpha = particle.params2.z;
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
                particle.color.a = alpha;
                gParticles[index] = particle;
                AppendActiveParticle(index, particleCount, particle);
            }
        }

        return;
    }

    if (emitMode != 0u && index < emitterConfig.z)
    {
        int freeListIndex = 0;
        InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);
        if (freeListIndex <= 0)
        {
            InterlockedAdd(gFreeListIndex[0], 1);
        } else if (freeListIndex > (int) particleCount)
        {
            InterlockedAdd(gFreeListIndex[0], 1);
        } else
        {
            uint particleIndex = gFreeList[freeListIndex - 1];
            if (particleIndex >= particleCount)
            {
                InterlockedAdd(gFreeListIndex[0], 1);
                return;
            }
            Particle respawnParticle = gParticles[particleIndex];
            Respawn(particleIndex, respawnParticle);
            gParticles[particleIndex] = respawnParticle;
            AppendActiveParticle(particleIndex, particleCount, respawnParticle);
        }
    }
}

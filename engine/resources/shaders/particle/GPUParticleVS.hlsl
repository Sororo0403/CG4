#include "GPUParticle.hlsli"

cbuffer ParticleDrawParams : register(b0)
{
    float4x4 viewProjection;
    float4 cameraRight;
    float4 cameraUp;
    float4 tintColor;
    float4 atlasInfo;
    float4 materialParams0;
    float4 materialParams1;
};

StructuredBuffer<Particle> gParticles : register(t0);
StructuredBuffer<uint> gActiveIndices : register(t3);

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
    Particle particle = gParticles[gActiveIndices[instanceId]];

    if (particle.isActive == 0u || particle.color.a <= 0.0001f)
    {
        ParticleVSOutput inactiveOutput;
        inactiveOutput.position = float4(0.0f, 0.0f, 0.0f, 1.0f);
        inactiveOutput.uv = float2(0.0f, 0.0f);
        inactiveOutput.localUv = float2(0.0f, 0.0f);
        inactiveOutput.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
        inactiveOutput.params = float2(1.0f, 0.0f);
        return inactiveOutput;
    }

    float ageRate = saturate(particle.currentTime / max(particle.lifeTime, 0.001f));

    float scale = lerp(particle.params0.x, particle.params0.y, ageRate);
    float stretch = max(0.0f, particle.params1.y);
    float2 localScale = float2(scale * (1.0f + stretch), scale);
    float2 local = kPositions[quadVertexId] * localScale;

    float roll = particle.params1.w + particle.currentTime * particle.scale.y;
    float s = sin(roll);
    float c = cos(roll);
    local = float2(local.x * c - local.y * s, local.x * s + local.y * c);

    float3 worldPosition =
        particle.translate +
        cameraRight.xyz * local.x +
        cameraUp.xyz * local.y;

    ParticleVSOutput output;
    output.position = mul(float4(worldPosition, 1.0f), viewProjection);
    uint atlasColumns = max(1u, (uint) round(particle.params2.w));
    uint atlasRows = max(1u, (uint) round(particle.params3.w));
    uint atlasFrameCount = atlasColumns * atlasRows;
    uint frameIndex =
        atlasFrameCount > 0u
            ? ((uint) max(0.0f, floor(particle.scale.x + 0.5f))) %
                  atlasFrameCount
            : 0u;
    float2 atlasScale = 1.0f / float2((float) atlasColumns, (float) atlasRows);
    float2 atlasOffset =
        float2((float) (frameIndex % atlasColumns),
               (float) (frameIndex / atlasColumns)) *
        atlasScale;

    output.uv = atlasOffset + kUvs[quadVertexId] * atlasScale;
    output.localUv = kUvs[quadVertexId];
    output.color = particle.color * tintColor;
    output.params = float2(ageRate, particle.params1.z);
    return output;
}

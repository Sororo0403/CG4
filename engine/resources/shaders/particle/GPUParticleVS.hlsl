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
        inactiveOutput.worldPosition = float3(0.0f, 0.0f, 0.0f);
        return inactiveOutput;
    }

    float ageRate = saturate(particle.currentTime / max(particle.lifeTime, 0.001f));

    float scale = lerp(particle.params0.x, particle.params0.y, ageRate);
    float stretch = max(0.0f, particle.params1.y);
    float verticalStretch = step(0.5f, materialParams0.y);
    float2 horizontalScale = float2(scale * (1.0f + stretch), scale);
    float2 verticalScale = float2(scale, scale * (1.0f + stretch));
    float2 localScale = lerp(horizontalScale, verticalScale, verticalStretch);
    float2 local = kPositions[quadVertexId] * localScale;

    float orientationMode = materialParams1.x;
    float velocityAligned =
        step(0.5f, orientationMode) * (1.0f - step(1.5f, orientationMode));
    float radialAligned =
        step(1.5f, orientationMode) * (1.0f - step(2.5f, orientationMode));
    float worldWindAligned =
        step(2.5f, orientationMode) * (1.0f - step(3.5f, orientationMode));
    float worldTrailAligned =
        step(3.5f, orientationMode) * (1.0f - step(4.5f, orientationMode));
    float3 worldOffset = float3(0.0f, 0.0f, 0.0f);
    float2 velocityScreen =
        float2(dot(particle.velocity, cameraRight.xyz),
               dot(particle.velocity, cameraUp.xyz));
    float velocityLengthSq = dot(velocityScreen, velocityScreen);
    if (worldTrailAligned > 0.5f)
    {
        float3 along = particle.velocity;
        float alongLengthSq = dot(along, along);
        along = alongLengthSq > 0.000001f
                    ? along * rsqrt(alongLengthSq)
                    : float3(1.0f, 0.0f, 0.0f);

        float3 drawAxis = particle.params4.xyz;
        float drawAxisLengthSq = dot(drawAxis, drawAxis);
        drawAxis = drawAxisLengthSq > 0.000001f
                       ? drawAxis * rsqrt(drawAxisLengthSq)
                       : float3(0.0f, 1.0f, 0.0f);

        float3 across = cross(drawAxis, along);
        float acrossLengthSq = dot(across, across);
        if (acrossLengthSq <= 0.000001f)
        {
            across = cross(float3(0.0f, 1.0f, 0.0f), along);
            acrossLengthSq = dot(across, across);
        }
        if (acrossLengthSq <= 0.000001f)
        {
            across = cross(float3(1.0f, 0.0f, 0.0f), along);
            acrossLengthSq = dot(across, across);
        }
        across = acrossLengthSq > 0.000001f
                     ? across * rsqrt(acrossLengthSq)
                     : float3(0.0f, 0.0f, 1.0f);

        float3 axisX = lerp(along, across, verticalStretch);
        float3 axisY = lerp(across, along, verticalStretch);
        worldOffset = axisX * local.x + axisY * local.y;
    }
    else if (worldWindAligned > 0.5f)
    {
        float thinWindBand = step(particle.params1.z, 0.34f);
        float broadWindBand = step(0.76f, particle.params1.z);
        float windThickness =
            lerp(lerp(0.68f, 0.46f, thinWindBand), 0.96f, broadWindBand);
        local.y *= windThickness;
        local.x *= lerp(1.0f, 1.08f, thinWindBand) *
                   lerp(1.0f, 0.92f, broadWindBand);

        float3 windAxis = materialParams1.yzw;
        windAxis.y = 0.0f;
        float windAxisLengthSq = dot(windAxis, windAxis);
        if (windAxisLengthSq <= 0.000001f)
        {
            windAxis = float3(particle.velocity.x, 0.0f, particle.velocity.z);
            windAxisLengthSq = dot(windAxis, windAxis);
        }
        windAxis =
            windAxisLengthSq > 0.000001f
                ? windAxis * rsqrt(windAxisLengthSq)
                : float3(1.0f, 0.0f, 0.0f);
        float3 windSide = float3(windAxis.z, 0.0f, -windAxis.x);
        float waveFade = smoothstep(0.06f, 0.24f, ageRate) *
                         (1.0f - smoothstep(0.82f, 1.0f, ageRate));
        float wavePhase =
            particle.currentTime * (1.58f + particle.params1.z * 0.86f) +
            particle.seed * 0.013f + local.x * 1.46f;
        float sideWave =
            sin(wavePhase) +
            sin(wavePhase * 1.92f + particle.params1.z * 5.4f) * 0.42f;
        float verticalWave =
            sin(wavePhase * 0.73f + particle.params1.z * 6.2831853f);
        float windSpeedRate = saturate(materialParams0.z);
        float pathPhase =
            particle.currentTime * (0.74f + windSpeedRate * 0.52f) +
            particle.seed * 0.021f + ageRate * 2.80f;
        float pathWave =
            sin(pathPhase) +
            sin(pathPhase * 1.71f + particle.params1.z * 4.7f) * 0.35f;
        float sideAmplitude =
            (0.050f + scale * 0.48f) *
            lerp(0.88f, 1.18f, saturate((windThickness - 0.46f) / 0.50f));
        float pathAmplitude =
            (0.045f + scale * 0.30f) *
            lerp(0.72f, 1.14f, windSpeedRate) *
            lerp(0.60f, 1.0f, saturate(materialParams0.x));
        float verticalAmplitude = 0.014f + scale * 0.080f;
        worldOffset = windAxis * local.x + float3(0.0f, 1.0f, 0.0f) * local.y;
        worldOffset += windSide * (pathWave * pathAmplitude * waveFade);
        worldOffset += windSide * (sideWave * sideAmplitude * waveFade);
        worldOffset +=
            float3(0.0f, 1.0f, 0.0f) *
            (verticalWave * verticalAmplitude * waveFade);
    }
    else if (velocityAligned > 0.5f && velocityLengthSq > 0.000001f)
    {
        float2 along = velocityScreen * rsqrt(velocityLengthSq);
        float2 across = float2(-along.y, along.x);
        float2 axisX = lerp(along, across, verticalStretch);
        float2 axisY = lerp(across, along, verticalStretch);
        float2 orientedLocal = axisX * local.x + axisY * local.y;
        local = orientedLocal;
        worldOffset = cameraRight.xyz * local.x + cameraUp.xyz * local.y;
    }
    else if (radialAligned > 0.5f)
    {
        float3 cameraPosition = materialParams1.yzw;
        float3 cameraToParticle = particle.translate - cameraPosition;
        float2 radialScreen =
            float2(dot(cameraToParticle, cameraRight.xyz),
                   dot(cameraToParticle, cameraUp.xyz));
        float radialLengthSq = dot(radialScreen, radialScreen);
        if (radialLengthSq > 0.000001f)
        {
            float2 along = radialScreen * rsqrt(radialLengthSq);
            float2 across = float2(-along.y, along.x);
            local = along * local.x + across * local.y;
            worldOffset = cameraRight.xyz * local.x + cameraUp.xyz * local.y;
        }
        else
        {
            float roll =
                particle.params1.w + particle.currentTime * particle.scale.y;
            float s = sin(roll);
            float c = cos(roll);
            local =
                float2(local.x * c - local.y * s,
                       local.x * s + local.y * c);
            worldOffset = cameraRight.xyz * local.x + cameraUp.xyz * local.y;
        }
    }
    else
    {
        float roll = particle.params1.w + particle.currentTime * particle.scale.y;
        float s = sin(roll);
        float c = cos(roll);
        local = float2(local.x * c - local.y * s, local.x * s + local.y * c);
        worldOffset = cameraRight.xyz * local.x + cameraUp.xyz * local.y;
    }

    float3 worldPosition = particle.translate + worldOffset;

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
    output.worldPosition = worldPosition;
    return output;
}

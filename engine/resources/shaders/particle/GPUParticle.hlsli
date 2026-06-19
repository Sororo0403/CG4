#ifndef GPU_PARTICLE_HLSLI
#define GPU_PARTICLE_HLSLI

struct Particle
{
    float3 translate;
    float currentTime;
    float3 velocity;
    float lifeTime;
    float4 color;
    float2 scale;
    float seed;
    uint isActive;
    float4 params0;
    float4 params1;
    float4 params2;
    float4 params3;
    float4 params4;
};

struct ParticleVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float2 params : TEXCOORD1;
    float2 localUv : TEXCOORD2;
    float3 worldPosition : TEXCOORD3;
};

#endif // GPU_PARTICLE_HLSLI

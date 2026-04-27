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
    float padding;
};

struct ParticleVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

#endif // GPU_PARTICLE_HLSLI

#ifndef SPRITE_HLSLI
#define SPRITE_HLSLI

struct SpriteVSInput
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

struct SpriteVSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

#endif // SPRITE_HLSLI

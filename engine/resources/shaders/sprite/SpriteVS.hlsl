#include "Sprite.hlsli"

cbuffer SpriteCB : register(b0)
{
    float4x4 mat;
};

SpriteVSOutput main(SpriteVSInput input)
{
    SpriteVSOutput o;
    o.pos = mul(float4(input.pos, 1.0f), mat);
    o.uv = input.uv;
    o.color = input.color;
    return o;
}

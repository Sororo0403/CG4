#include "Sprite.hlsli"

Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);

float4 main(SpriteVSOutput input) : SV_TARGET
{
    float4 texColor = tex0.Sample(samp0, input.uv);
    return texColor * input.color;
}

#include "Sprite.hlsli"

Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);

float4 main(SpriteVSOutput input) : SV_TARGET
{
    float4 texColor = tex0.Sample(samp0, input.uv);
    return texColor * input.color;
}

float4 mainModulate(SpriteVSOutput input) : SV_TARGET
{
    float alphaMask = tex0.Sample(samp0, input.uv).a * input.color.a;
    alphaMask = saturate(alphaMask * 1.85f);
    float3 modulation = lerp(float3(1.0f, 1.0f, 1.0f), input.color.rgb, alphaMask);
    return float4(modulation, alphaMask);
}

float4 mainPremultipliedMask(SpriteVSOutput input) : SV_TARGET
{
    float alphaMask = tex0.Sample(samp0, input.uv).a * input.color.a;
    alphaMask = saturate(alphaMask * 2.35f);
    float3 premultipliedTint = input.color.rgb * alphaMask;
    return float4(premultipliedTint, alphaMask);
}

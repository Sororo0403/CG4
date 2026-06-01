#include "PostProcessCommon.hlsli"

Texture2D renderTexture : register(t0);
SamplerState textureSampler : register(s0);

float4 main(PostProcessVSOutput input) : SV_TARGET
{
    return renderTexture.Sample(textureSampler, input.uv);
}

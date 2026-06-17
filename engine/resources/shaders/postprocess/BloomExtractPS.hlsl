#include "BloomPassCommon.hlsli"

float4 main(BloomPassVSOutput input) : SV_TARGET
{
    float3 color = Downsample13(input.uv);
    return float4(ApplySoftThreshold(color), 1.0f);
}

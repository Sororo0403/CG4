#include "BloomPassCommon.hlsli"

float4 main(BloomPassVSOutput input) : SV_TARGET
{
    return float4(Downsample13(input.uv), 1.0f);
}

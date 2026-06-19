#include "PostProcessCommon.hlsli"

Texture2D<float4> currentVolume : register(t0);
Texture2D<float4> historyVolume : register(t1);
SamplerState linearClampSampler : register(s0);

cbuffer VolumetricLightingCompositeConstants : register(b0)
{
    float4 compositeParams;
};

float3 SharpenCurrent(float2 uv)
{
    uint width = 1;
    uint height = 1;
    currentVolume.GetDimensions(width, height);
    float2 texel = 1.0f / max(float2(width, height), float2(1.0f, 1.0f));

    float3 center = currentVolume.SampleLevel(linearClampSampler, uv, 0.0f).rgb;
    float3 neighbors =
        currentVolume.SampleLevel(linearClampSampler, uv + float2(texel.x, 0.0f), 0.0f).rgb +
        currentVolume.SampleLevel(linearClampSampler, uv - float2(texel.x, 0.0f), 0.0f).rgb +
        currentVolume.SampleLevel(linearClampSampler, uv + float2(0.0f, texel.y), 0.0f).rgb +
        currentVolume.SampleLevel(linearClampSampler, uv - float2(0.0f, texel.y), 0.0f).rgb;
    float3 blur = neighbors * 0.25f;
    float sharpen = saturate(compositeParams.y);
    return max(center + (center - blur) * sharpen, 0.0f);
}

float4 main(PostProcessVSOutput input) : SV_TARGET
{
    float historyBlend = saturate(compositeParams.x);
    float3 current = SharpenCurrent(input.uv);
    float3 history = historyVolume.SampleLevel(linearClampSampler, input.uv, 0.0f).rgb;
    return float4(lerp(current, history, historyBlend), 0.0f);
}

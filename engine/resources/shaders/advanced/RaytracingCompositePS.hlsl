#include "../postprocess/PostProcessCommon.hlsli"

Texture2D<float4> gRaytracingPreview : register(t0);
SamplerState gSampler : register(s0);

float4 main(PostProcessVSOutput input) : SV_TARGET
{
    const float2 panelMin = float2(0.675f, 0.065f);
    const float2 panelSize = float2(0.275f, 0.275f);
    const float2 local = (input.uv - panelMin) / panelSize;
    if (any(local < 0.0f) || any(local > 1.0f))
    {
        discard;
    }

    const float2 centered = abs(local - 0.5f) * 2.0f;
    const float border = smoothstep(0.96f, 0.90f, max(centered.x, centered.y));
    const float4 sampleColor = gRaytracingPreview.Sample(gSampler, local);
    const float3 frameColor = float3(0.08f, 0.15f, 0.20f);
    const float3 color = lerp(frameColor, sampleColor.rgb, saturate(sampleColor.a + 0.28f));
    return float4(color, 0.58f * border);
}

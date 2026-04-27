#include "GPUParticle.hlsli"

Texture2D particleTexture : register(t1);
SamplerState particleSampler : register(s0);

float StarMask(float2 p)
{
    float angle = atan2(p.y, p.x);
    float radius = length(p);
    float points = pow(cos(angle * 5.0f) * 0.5f + 0.5f, 1.45f);
    float edge = lerp(0.28f, 0.76f, points);
    return 1.0f - smoothstep(edge - 0.030f, edge + 0.030f, radius);
}

float ScallopMask(float2 p)
{
    float radius = length(p);
    float wobble = sin(atan2(p.y, p.x) * 6.0f) * 0.055f;
    return 1.0f - smoothstep(0.58f + wobble, 0.64f + wobble, radius);
}

float PetalMask(float2 p)
{
    p.y += 0.04f;
    float left = length((p - float2(-0.16f, 0.08f)) / float2(0.48f, 0.42f));
    float right = length((p - float2(0.16f, 0.08f)) / float2(0.48f, 0.42f));
    float bottom = length((p - float2(0.0f, -0.20f)) / float2(0.38f, 0.52f));
    float mask = min(min(left, right), bottom);
    return 1.0f - smoothstep(0.94f, 1.02f, mask);
}

float BoltMask(float2 p)
{
    p.x += 0.05f;
    float d0 = abs(p.x + p.y * 0.28f) - 0.13f;
    float d1 = abs(p.x - p.y * 0.34f) - 0.16f;
    float cut = step(-0.18f, p.y) * step(p.y, 0.62f);
    float tail = step(-0.64f, p.y) * step(p.y, 0.06f);
    return saturate((1.0f - smoothstep(0.0f, 0.055f, min(d0, d1))) * max(cut, tail));
}

float4 main(ParticleVSOutput input) : SV_TARGET
{
    float2 p = input.uv * 2.0f - 1.0f;
    float selector = input.params.x;
    float fillMask = selector < 0.30f ? StarMask(p) :
                     selector < 0.58f ? ScallopMask(p) :
                     selector < 0.84f ? PetalMask(p) :
                                         BoltMask(p);
    float outlineMask = selector < 0.30f ? StarMask(p * 0.87f) :
                        selector < 0.58f ? ScallopMask(p * 0.87f) :
                        selector < 0.84f ? PetalMask(p * 0.87f) :
                                            BoltMask(p * 0.87f);
    float outline = saturate(outlineMask - fillMask);
    float paperGrain = 0.94f + 0.06f * sin((input.uv.x + input.uv.y) * 58.0f + input.params.x * 19.0f);
    float gloss = smoothstep(0.20f, 0.95f, 1.0f - length(p - float2(-0.24f, 0.28f))) * 0.18f;

    float3 fill = saturate(input.color.rgb * (1.04f + gloss)) * paperGrain;
    float3 edge = lerp(float3(1.0f, 0.96f, 0.84f), float3(0.12f, 0.10f, 0.18f),
                       step(0.74f, selector) * 0.35f);
    float alpha = saturate(fillMask + outline) * input.color.a;
    float3 rgb = lerp(fill, edge, outline * 0.95f);
    return float4(rgb, alpha);
}

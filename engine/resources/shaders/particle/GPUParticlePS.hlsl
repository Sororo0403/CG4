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

float DirectionalMask(float2 p)
{
    p.x *= 0.32f;
    float core = 1.0f - smoothstep(0.02f, 0.14f, abs(p.y));
    float tip = 1.0f - smoothstep(0.58f, 1.02f, abs(p.x));
    float hot = 1.0f - smoothstep(0.02f, 0.34f, length(p / float2(0.42f, 0.20f)));
    return saturate(max(core * tip, hot));
}

float PulseMask(float2 p, float ageRate)
{
    float radius = length(p);
    float cross = max(1.0f - smoothstep(0.020f, 0.15f, abs(p.y)),
                      1.0f - smoothstep(0.020f, 0.15f, abs(p.x)));
    float diagA = 1.0f - smoothstep(0.012f, 0.11f,
                                    abs(p.y - p.x * 0.55f));
    float diagB = 1.0f - smoothstep(0.012f, 0.11f,
                                    abs(p.y + p.x * 0.55f));
    float core = 1.0f - smoothstep(0.03f, 0.25f, radius);
    float fade = saturate(1.0f - ageRate);
    return saturate((max(cross, max(diagA, diagB)) *
                         (1.0f - smoothstep(0.44f, 1.0f, radius)) +
                     core) *
                    fade);
}

float RadialMask(float2 p, float ageRate)
{
    float radius = length(p);
    float angle = atan2(p.y, p.x);
    float noise = sin(angle * 9.0f + ageRate * 7.0f) * 0.070f +
                  sin(angle * 17.0f - ageRate * 4.0f) * 0.035f;
    float edge = 0.36f + ageRate * 0.38f + noise;
    float hotShell = 1.0f - smoothstep(edge - 0.10f, edge + 0.09f, radius);
    float core = 1.0f - smoothstep(0.02f, 0.24f + ageRate * 0.18f, radius);
    float ring = smoothstep(edge - 0.18f, edge - 0.03f, radius) *
                 (1.0f - smoothstep(edge + 0.02f, edge + 0.14f, radius));
    return saturate(max(max(hotShell * (1.0f - ageRate * 0.18f), core), ring * 0.86f));
}

float BuoyantMask(float2 p, float ageRate)
{
    float radius = length(p);
    float angle = atan2(p.y, p.x);
    float noise = sin(angle * 5.0f + ageRate * 2.0f) * 0.10f +
                  sin((p.x - p.y) * 8.0f + ageRate * 5.0f) * 0.045f;
    float edge = 0.46f + ageRate * 0.30f + noise;
    float body = 1.0f - smoothstep(edge - 0.16f, edge + 0.12f, radius);
    float centerBreak = smoothstep(0.05f, 0.36f + ageRate * 0.18f, radius);
    return saturate(body * lerp(0.78f, 1.0f, centerBreak));
}

float4 main(ParticleVSOutput input) : SV_TARGET
{
    float2 p = input.uv * 2.0f - 1.0f;
    float emissionMode = input.params.x;
    float ageRate = input.params.y;
    float fillMask = emissionMode < 0.5f ? DirectionalMask(p)
                    : emissionMode < 1.5f ? RadialMask(p, ageRate)
                    : emissionMode > 2.5f && emissionMode < 3.5f ? PulseMask(p, ageRate)
                                                                 : BuoyantMask(p, ageRate);
    float outlineMask = emissionMode < 0.5f ? DirectionalMask(p * 0.84f)
                        : emissionMode < 1.5f ? RadialMask(p * 0.88f, ageRate)
                        : emissionMode > 2.5f && emissionMode < 3.5f ? PulseMask(p * 0.90f, ageRate)
                                                                     : BuoyantMask(p * 0.90f, ageRate);
    float outline = saturate(outlineMask - fillMask);
    float grain = 0.92f + 0.08f * sin((input.uv.x + input.uv.y) * 78.0f + ageRate * 31.0f);
    float metallicHotspot =
        smoothstep(0.10f, 0.95f, 1.0f - length(p - float2(-0.22f, 0.22f)));

    float3 base = saturate(input.color.rgb);
    float3 hotCore = emissionMode < 0.5f ? saturate(base * 1.45f + 0.10f)
                     : emissionMode < 1.5f ? saturate(base * 1.20f + 0.08f)
                     : emissionMode > 2.5f && emissionMode < 3.5f ? saturate(base * 1.55f + 0.18f)
                                                                  : saturate(base * 0.55f);
    float3 fill = saturate(input.color.rgb * grain + hotCore * metallicHotspot * 0.42f);
    if (emissionMode > 1.5f)
    {
        fill = lerp(fill, base * 0.42f, saturate(ageRate * 0.85f));
    }
    float3 edge = emissionMode < 0.5f ? saturate(base * 1.35f + 0.08f)
                  : emissionMode < 1.5f ? base * 0.36f
                  : emissionMode > 2.5f && emissionMode < 3.5f ? saturate(base * 1.15f + 0.12f)
                                                               : base * 0.24f;
    float alpha = saturate(fillMask + outline) * input.color.a;
    float3 rgb = lerp(fill, edge, outline * (emissionMode < 0.5f ? 0.30f :
                                             emissionMode < 1.5f ? 0.76f : 0.55f));
    float4 texel = particleTexture.Sample(particleSampler, input.uv);
    float textureLuma = dot(texel.rgb, float3(0.299f, 0.587f, 0.114f));
    rgb *= lerp(0.72f, 1.12f, textureLuma);
    alpha *= lerp(0.35f, 1.0f, texel.a);
    return float4(rgb, alpha);
}

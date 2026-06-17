#include "GPUParticle.hlsli"

Texture2D particleTexture : register(t1);
SamplerState particleSampler : register(s0);

#define SHAPE_SOFT_CIRCLE 0.0f
#define SHAPE_RING 1.0f
#define SHAPE_SPARK 2.0f
#define SHAPE_SLASH 3.0f
#define SHAPE_SMOKE 4.0f

float Hash(float2 p)
{
    return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f);
}

float Noise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);

    float a = Hash(i);
    float b = Hash(i + float2(1.0f, 0.0f));
    float c = Hash(i + float2(0.0f, 1.0f));
    float d = Hash(i + float2(1.0f, 1.0f));

    float2 u = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float EaseOut(float t)
{
    return 1.0f - (1.0f - t) * (1.0f - t);
}

float CreateSoftCircle(float2 uv, float radius, float softness)
{
    float d = length(uv);
    return 1.0f - smoothstep(radius, radius + softness, d);
}

float CreateRing(float2 uv, float radius, float thickness, float softness)
{
    float d = length(uv);
    float ring = 1.0f - abs(d - radius) / max(thickness, 0.0001f);
    ring = saturate(ring);
    return smoothstep(0.0f, max(softness, 0.0001f), ring);
}

float CreateSpark(float2 uv, float width, float lengthScale, float sharpness)
{
    uv.y *= max(lengthScale, 0.0001f);
    float body = saturate(1.0f - abs(uv.x) / max(width, 0.0001f));
    float len = saturate(1.0f - abs(uv.y));
    return pow(saturate(body * len), max(sharpness, 0.0001f));
}

float CreateSlash(float2 uv)
{
    uv.x *= 0.62f;
    uv.y *= 1.55f;
    float curve = uv.y - uv.x * uv.x * 0.55f + 0.18f;
    float stroke = saturate(1.0f - abs(curve) * 18.0f);
    float endFade = saturate(1.0f - abs(uv.x));
    float innerCut = smoothstep(-0.74f, 0.22f, uv.y + uv.x * 0.22f);
    return pow(saturate(stroke * endFade * innerCut), 1.5f);
}

float CreateSmoke(float2 uv, float noiseScale, float distortionPower, float edgeSoftness, float seed)
{
    float n = Noise(uv * max(noiseScale, 0.0001f) + seed);
    float d = length(uv);
    d += (n - 0.5f) * distortionPower;
    return 1.0f - smoothstep(0.35f, 0.35f + max(edgeSoftness, 0.0001f), d);
}

float4 main(ParticleVSOutput input) : SV_TARGET
{
    float2 uv = input.localUv * 2.0f - 1.0f;
    float lifeT = saturate(input.params.x);
    float seed = input.params.y;
    float shapeType = input.style.x;
    float4 shapeParams = input.shapeParams;
    float4 textureColor = particleTexture.Sample(particleSampler, input.uv);

    float alpha = 0.0f;

    if (shapeType < 0.5f)
    {
        alpha = CreateSoftCircle(uv, shapeParams.x, shapeParams.y);
        alpha *= 1.0f - smoothstep(0.2f, 1.0f, lifeT);
        alpha *= max(shapeParams.z, 0.0f);
    }
    else if (shapeType < 1.5f)
    {
        float radius = lerp(shapeParams.x, shapeParams.y, EaseOut(lifeT));
        alpha = CreateRing(uv, radius, shapeParams.z, shapeParams.w);
        alpha *= 1.0f - lifeT;
    }
    else if (shapeType < 2.5f)
    {
        alpha = CreateSpark(uv, shapeParams.x, shapeParams.y, shapeParams.z);
        alpha *= pow(1.0f - lifeT, 1.5f);
    }
    else if (shapeType < 3.5f)
    {
        alpha = CreateSlash(uv);
        alpha *= pow(1.0f - lifeT, 1.3f);
    }
    else
    {
        alpha = CreateSmoke(uv, shapeParams.x, shapeParams.y, shapeParams.z, seed);
        float fadeIn = smoothstep(0.0f, 0.15f, lifeT);
        float fadeOut = 1.0f - smoothstep(0.55f, 1.0f, lifeT);
        alpha *= fadeIn * fadeOut;
    }

    float isSmoke = step(3.5f, shapeType);
    float textureAlpha = max(textureColor.a, max(textureColor.r, max(textureColor.g, textureColor.b)));
    alpha *= textureAlpha;

    float3 baseColor = max(input.color.rgb, 0.0f);
    float textureLuma = dot(saturate(textureColor.rgb), float3(0.299f, 0.587f, 0.114f));
    float innerGlow = smoothstep(0.52f, 0.96f, textureLuma) *
                      pow(saturate(1.0f - lifeT * 0.65f), 0.75f);
    float3 textureTint = lerp(float3(1.0f, 1.0f, 1.0f),
                              saturate(textureColor.rgb) * 1.35f,
                              saturate(textureAlpha));
    float hotCore = CreateSoftCircle(uv, 0.12f, 0.24f);
    float3 litColor = lerp(baseColor, baseColor * (1.35f + (1.0f - lifeT) * 0.85f), hotCore);
    litColor *= lerp(0.78f, 1.45f, saturate(textureAlpha));
    litColor += float3(1.35f, 0.78f, 1.95f) * innerGlow * (1.0f - isSmoke);
    float3 smokeColor = lerp(baseColor * 0.72f, float3(0.50f, 0.58f, 0.62f), 0.35f);
    smokeColor *= lerp(0.55f, 1.10f, saturate(textureAlpha));
    smokeColor += float3(0.35f, 0.08f, 0.60f) * innerGlow * 0.35f;
    float3 rgb = lerp(litColor, smokeColor, isSmoke);
    rgb *= textureTint;

    return float4(rgb, input.color.a * alpha);
}

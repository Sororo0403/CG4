#include "../GPUParticle.hlsli"

cbuffer ParticleDrawParams : register(b0)
{
    float4x4 viewProjection;
    float4 cameraRight;
    float4 cameraUp;
    float4 tintColor;
    float4 atlasInfo;
    float4 materialParams0;
    float4 materialParams1;
};

SamplerState particleSampler : register(s0);

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);

    float a = Hash12(i);
    float b = Hash12(i + float2(1.0f, 0.0f));
    float c = Hash12(i + float2(0.0f, 1.0f));
    float d = Hash12(i + float2(1.0f, 1.0f));

    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float RotateNoise(float2 p, float randomValue, float scale)
{
    float n0 = ValueNoise(p * scale + randomValue * 17.0f);
    float n1 = ValueNoise(p * scale * 2.13f - randomValue * 9.0f);
    return lerp(n0, n1, 0.35f);
}

float SoftEdge(float distanceValue, float softness)
{
    float width = 0.050f + softness * 0.19f;
    return 1.0f - smoothstep(0.0f, width, distanceValue);
}

float CottonPuff(float2 p, float randomValue, float softness, float detailScale)
{
    float radius = length(p);
    float angle = atan2(p.y, p.x);
    float low = sin(angle * 5.0f + randomValue * 11.0f) * 0.030f;
    low += sin(angle * 3.0f - randomValue * 7.0f) * 0.022f;
    float high = RotateNoise(p + float2(angle, radius), randomValue,
                             detailScale) *
                     0.070f -
                 0.035f;
    float edge = 0.64f + low + high;
    float body =
        1.0f - smoothstep(edge, edge + 0.20f + softness * 0.24f, radius);
    float plumpCore = 1.0f - smoothstep(0.08f, 0.54f, radius);
    float softHalo = 1.0f - smoothstep(0.52f, 0.98f, radius);
    return saturate(max(body, plumpCore * 0.24f) + softHalo * 0.08f);
}

float PressureRing(float2 p, float randomValue, float softness, float detailScale)
{
    float radius = length(p);
    float angle = atan2(p.y, p.x);
    float noise = sin(angle * 11.0f + randomValue * 16.0f) * 0.025f;
    noise += (RotateNoise(p, randomValue, detailScale) - 0.5f) * 0.055f;
    float ringCenter = 0.56f + noise;
    float ringWidth = 0.070f + softness * 0.090f;
    float ring = 1.0f - smoothstep(ringWidth, ringWidth + 0.13f,
                                   abs(radius - ringCenter));
    float outerFade = 1.0f - smoothstep(0.82f, 1.08f, radius);
    return ring * outerFade;
}

float FeltShard(float2 p, float randomValue, float softness, float detailScale)
{
    float skew = (RotateNoise(p, randomValue, detailScale) - 0.5f) * 0.10f;
    p.x += p.y * 0.24f + skew;
    p.y *= 1.16f;
    float roundedFelt = pow(abs(p.x), 2.8f) + pow(abs(p.y), 2.8f);
    float chipped =
        (RotateNoise(p + 3.0f, randomValue, detailScale) - 0.5f) * 0.060f;
    return SoftEdge(roundedFelt - 0.38f + chipped, softness);
}

float StitchDash(float2 p, float randomValue, float softness, float dashCount)
{
    p.x += sin(p.y * 5.0f + randomValue * 6.0f) * 0.05f;
    float count = max(2.0f, dashCount);
    float cell = frac((p.x * 0.5f + 0.5f) * count);
    float dashBody = 1.0f - smoothstep(0.30f, 0.49f, abs(cell - 0.5f));
    float stitchLine =
        1.0f - smoothstep(0.040f, 0.112f + softness * 0.040f, abs(p.y));
    float endFade = 1.0f - smoothstep(0.72f, 1.02f, abs(p.x));
    return dashBody * stitchLine * endFade;
}

float ThreadWisp(float2 p, float randomValue, float softness, float detailScale)
{
    float wave = sin(p.x * 7.0f + randomValue * 10.0f) * 0.040f;
    float strandA = 1.0f - smoothstep(0.016f, 0.064f + softness * 0.036f,
                                      abs(p.y - wave));
    float strandB = 1.0f - smoothstep(0.012f, 0.040f + softness * 0.024f,
                                      abs(p.y - wave - 0.105f));
    float strandC = 1.0f - smoothstep(0.010f, 0.034f + softness * 0.020f,
                                      abs(p.y - wave + 0.100f));
    float endFade = 1.0f - smoothstep(0.62f, 1.0f, abs(p.x));
    float fiberBreak = lerp(0.72f, 1.12f,
                            RotateNoise(p, randomValue, detailScale * 1.6f));
    return saturate(max(strandA, max(strandB * 0.54f, strandC * 0.40f)) *
                    endFade * fiberBreak);
}

float PlushSlash(float2 p, float randomValue, float softness, float detailScale)
{
    p.y += p.x * 0.23f;
    float curve = sin(p.x * 3.0f + randomValue * 4.0f) * 0.050f;
    curve += sin(p.x * 8.0f - randomValue * 2.0f) * 0.012f;
    float center = p.y - curve;
    float taper = 1.0f - smoothstep(0.50f, 1.05f, abs(p.x));
    float plump = 0.28f + taper * 0.72f;
    float edgeNoise =
        (RotateNoise(p * float2(0.72f, 1.65f), randomValue,
                     detailScale * 0.34f) -
         0.5f) *
        0.030f;
    float width = (0.076f + softness * 0.090f) * plump;
    float sharpCore =
        1.0f - smoothstep(width * 0.30f, width * 0.30f + 0.026f,
                          abs(center));
    float blade =
        1.0f - smoothstep(width + edgeNoise,
                          width + 0.076f + softness * 0.040f, abs(center));
    float cottonEdge =
        1.0f - smoothstep(width + 0.020f,
                          width + 0.260f + softness * 0.090f, abs(center));
    float fluffyRidge = pow(saturate(cottonEdge - blade * 0.35f), 1.8f);
    fluffyRidge *= lerp(0.86f, 1.18f,
                        RotateNoise(p + 2.7f, randomValue,
                                    detailScale * 0.55f));
    float fiberBreak =
        lerp(0.78f, 1.18f, RotateNoise(p, randomValue, detailScale));
    float seamPulse = frac((p.x * 0.5f + 0.5f) * 9.0f + randomValue * 0.17f);
    float seam =
        1.0f - smoothstep(0.18f, 0.43f, abs(seamPulse - 0.50f));
    seam *= 1.0f - smoothstep(width + 0.018f, width + 0.095f, abs(center));
    seam *= smoothstep(width * 0.55f, width + 0.045f, abs(center));
    return saturate((sharpCore * 0.72f + blade + cottonEdge * 0.40f +
                     fluffyRidge * 0.22f + seam * 0.20f) *
                    taper * fiberBreak);
}

float HeartPuff(float2 p, float randomValue, float softness, float detailScale)
{
    p.x *= 1.05f;
    p.y = p.y * 1.18f - 0.08f;
    float wobble = (RotateNoise(p + 1.7f, randomValue, detailScale * 0.55f) -
                    0.5f) *
                   0.040f;
    p.x += sin(p.y * 6.0f + randomValue * 8.0f) * 0.018f;

    float x = p.x;
    float y = p.y;
    float heart = pow(x * x + y * y - 0.34f + wobble, 3.0f) -
                  x * x * y * y * y;
    float body = 1.0f - smoothstep(-0.030f, 0.090f + softness * 0.16f, heart);
    float plushEdge =
        1.0f - smoothstep(0.54f, 1.04f + softness * 0.12f, length(p));
    float cleft = smoothstep(0.050f, 0.25f, abs(p.x)) +
                  smoothstep(0.12f, 0.34f, 0.38f - p.y);
    float highlight =
        1.0f - smoothstep(0.020f, 0.16f, length(p - float2(-0.17f, 0.22f)));
    return saturate(body * plushEdge * saturate(cleft) + highlight * 0.18f);
}

float SparkleStar(float2 p, float randomValue, float softness, float detailScale)
{
    p += (RotateNoise(p, randomValue, detailScale) - 0.5f) * 0.018f;
    float r = length(p);
    float angle = atan2(p.y, p.x);
    float rays = abs(cos(angle * 4.0f + randomValue * 1.7f));
    float longRay = 1.0f - smoothstep(0.015f, 0.070f + softness * 0.030f,
                                      min(abs(p.x), abs(p.y)));
    float diamond = 1.0f - smoothstep(0.22f, 0.42f + softness * 0.18f,
                                      abs(p.x) + abs(p.y));
    float glint = pow(saturate(1.0f - r), 4.0f) * 1.25f;
    float rayFade = 1.0f - smoothstep(0.18f, 0.95f, r);
    float twinkle = lerp(0.82f, 1.16f,
                         RotateNoise(p + angle, randomValue, detailScale));
    return saturate((diamond + longRay * rays * rayFade + glint) * twinkle);
}

float FiberShade(float2 p, float randomValue, float strength, float detailScale)
{
    float angle = atan2(p.y, p.x);
    float radius = length(p);
    float thread = sin((p.x * 0.72f + p.y * 1.18f + randomValue) *
                       detailScale * 5.5f);
    float radial = sin(angle * 15.0f + radius * detailScale * 3.0f +
                       randomValue * 13.0f);
    float noise = RotateNoise(p, randomValue, detailScale);
    float fiber = (thread * 0.5f + 0.5f) * 0.38f +
                  (radial * 0.5f + 0.5f) * 0.26f + noise * 0.36f;
    return lerp(1.0f, lerp(0.86f, 1.18f, fiber), saturate(strength));
}

float4 main(ParticleVSOutput input) : SV_TARGET
{
    float2 p = input.localUv * 2.0f - 1.0f;
    float ageRate = input.params.x;
    float randomValue = input.params.y;

    float shapeType = materialParams0.x;
    float edgeSoftness = saturate(materialParams0.y);
    float fiberStrength = saturate(materialParams0.z);
    float innerGlow = saturate(materialParams0.w);
    float detailScale = max(2.0f, materialParams1.x);
    float dashCount = max(2.0f, materialParams1.y);

    float mask = 0.0f;
    if (shapeType < 0.5f)
    {
        mask = CottonPuff(p, randomValue, edgeSoftness, detailScale);
    }
    else if (shapeType < 1.5f)
    {
        mask = PressureRing(p, randomValue, edgeSoftness, detailScale);
    }
    else if (shapeType < 2.5f)
    {
        mask = FeltShard(p, randomValue, edgeSoftness, detailScale);
    }
    else if (shapeType < 3.5f)
    {
        mask = StitchDash(p, randomValue, edgeSoftness, dashCount);
    }
    else if (shapeType < 4.5f)
    {
        mask = ThreadWisp(p, randomValue, edgeSoftness, detailScale);
    }
    else if (shapeType < 5.5f)
    {
        mask = PlushSlash(p, randomValue, edgeSoftness, detailScale);
    }
    else if (shapeType < 6.5f)
    {
        mask = HeartPuff(p, randomValue, edgeSoftness, detailScale);
    }
    else
    {
        mask = SparkleStar(p, randomValue, edgeSoftness, detailScale);
    }

    float radius = length(p);
    float core = 1.0f - smoothstep(0.0f, 0.55f, radius);
    float edge = smoothstep(0.30f, 0.88f, radius);
    float fiber = FiberShade(p, randomValue, fiberStrength, detailScale);
    float ageGlow = 1.0f - ageRate;

    float3 base = saturate(input.color.rgb);
    float3 rgb = base * fiber;
    rgb += base * core * innerGlow * (0.55f + ageGlow * 0.45f);
    rgb = lerp(rgb, sqrt(rgb), saturate(core * innerGlow * 0.32f));
    rgb = lerp(rgb, rgb * 1.10f, edge * fiberStrength * 0.28f);

    float alpha = saturate(mask) * input.color.a;
    alpha *= lerp(1.0f, 0.70f + fiber * 0.36f, fiberStrength);
    alpha *= saturate(1.0f - ageRate * 0.18f);

    return float4(saturate(rgb), saturate(alpha));
}

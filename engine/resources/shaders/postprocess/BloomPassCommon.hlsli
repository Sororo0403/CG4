#ifndef BLOOM_PASS_COMMON_HLSLI
#define BLOOM_PASS_COMMON_HLSLI

struct BloomPassVSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

cbuffer BloomPassConstants : register(b0)
{
    float2 sourceTexelSize;
    float2 targetTexelSize;
    float bloomPassThreshold;
    float bloomPassSoftKnee;
    float bloomPassRadius;
    float bloomPassIntensity;
};

Texture2D sourceTexture : register(t0);
SamplerState textureSampler : register(s0);

float3 SampleSource(float2 uv)
{
    return max(sourceTexture.Sample(textureSampler, uv).rgb, 0.0f);
}

float3 Downsample13(float2 uv)
{
    float2 x = float2(sourceTexelSize.x, 0.0f);
    float2 y = float2(0.0f, sourceTexelSize.y);

    float3 a = SampleSource(uv - x - y);
    float3 b = SampleSource(uv - y);
    float3 c = SampleSource(uv + x - y);
    float3 d = SampleSource(uv - x);
    float3 e = SampleSource(uv);
    float3 f = SampleSource(uv + x);
    float3 g = SampleSource(uv - x + y);
    float3 h = SampleSource(uv + y);
    float3 i = SampleSource(uv + x + y);

    float2 halfX = x * 0.5f;
    float2 halfY = y * 0.5f;
    float3 j = SampleSource(uv - halfX - halfY);
    float3 k = SampleSource(uv + halfX - halfY);
    float3 l = SampleSource(uv - halfX + halfY);
    float3 m = SampleSource(uv + halfX + halfY);

    return e * 0.125f +
           (b + d + f + h) * 0.0625f +
           (a + c + g + i) * 0.03125f +
           (j + k + l + m) * 0.125f;
}

float3 TentUpsample(float2 uv)
{
    float radius =
        lerp(0.85f, 1.35f, saturate((bloomPassRadius - 2.0f) / 5.0f));
    float2 x = float2(sourceTexelSize.x, 0.0f) * radius;
    float2 y = float2(0.0f, sourceTexelSize.y) * radius;

    float3 result = SampleSource(uv) * 4.0f;
    result += (SampleSource(uv - x) + SampleSource(uv + x) +
               SampleSource(uv - y) + SampleSource(uv + y)) * 2.0f;
    result += SampleSource(uv - x - y) + SampleSource(uv + x - y) +
              SampleSource(uv - x + y) + SampleSource(uv + x + y);
    return result * (1.0f / 16.0f);
}

float3 ApplySoftThreshold(float3 color)
{
    float brightness = max(max(color.r, color.g), color.b);
    float threshold = max(bloomPassThreshold, 0.0001f);
    float knee = max(threshold * saturate(bloomPassSoftKnee), 0.0001f);
    float soft = clamp(brightness - threshold + knee, 0.0f, knee * 2.0f);
    soft = soft * soft / (knee * 4.0f + 0.0001f);
    float contribution = max(brightness - threshold, soft);
    return color * (contribution / max(brightness, 0.0001f));
}

#endif // BLOOM_PASS_COMMON_HLSLI

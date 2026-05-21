#ifndef EDGE_EFFECT_HLSLI
#define EDGE_EFFECT_HLSLI

static const float2 kSobelOffsets[9] = {
    float2(-1.0f, -1.0f), float2(0.0f, -1.0f), float2(1.0f, -1.0f),
    float2(-1.0f, 0.0f),  float2(0.0f, 0.0f),  float2(1.0f, 0.0f),
    float2(-1.0f, 1.0f),  float2(0.0f, 1.0f),  float2(1.0f, 1.0f),
};

static const float kSobelX[9] = {
    -1.0f, 0.0f, 1.0f,
    -2.0f, 0.0f, 2.0f,
    -1.0f, 0.0f, 1.0f,
};

static const float kSobelY[9] = {
    -1.0f, -2.0f, -1.0f,
     0.0f,  0.0f,  0.0f,
     1.0f,  2.0f,  1.0f,
};

float CalcLuminance(float3 color)
{
    return dot(color, float3(0.2125f, 0.7154f, 0.0721f));
}

float LinearizeDepth(float depth)
{
    return (nearZ * farZ) / max(farZ - depth * (farZ - nearZ), 0.0001f);
}

float CalcLuminanceEdge(Texture2D sourceTexture, SamplerState sourceSampler,
                        float2 uv)
{
    float2 sobel = 0.0f;
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        float2 sampleUv = uv + kSobelOffsets[i] * texelSize;
        float luminance =
            CalcLuminance(sourceTexture.Sample(sourceSampler, sampleUv).rgb);
        sobel.x += luminance * kSobelX[i];
        sobel.y += luminance * kSobelY[i];
    }

    return length(sobel);
}

float CalcDepthEdge(Texture2D depthTexture, SamplerState sourceSampler,
                    float2 uv)
{
    float2 sobel = 0.0f;
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        float2 sampleUv = uv + kSobelOffsets[i] * texelSize;
        float depth =
            depthTexture.Sample(sourceSampler, sampleUv).r;
        float viewZ = LinearizeDepth(depth);
        sobel.x += viewZ * kSobelX[i];
        sobel.y += viewZ * kSobelY[i];
    }

    return length(sobel);
}

float4 ApplyEdgeEffect(float4 sourceColor, Texture2D sourceTexture,
                       Texture2D depthTexture, SamplerState sourceSampler,
                       float2 uv, int mode)
{
    if (mode == 1)
    {
        float edge = CalcLuminanceEdge(sourceTexture, sourceSampler, uv);
        if (edge > luminanceEdgeThreshold)
        {
            return float4(0.0f, 0.0f, 0.0f, sourceColor.a);
        }
    }
    else if (mode == 2)
    {
        float edge = CalcDepthEdge(depthTexture, sourceSampler, uv);
        if (edge > depthEdgeThreshold)
        {
            return float4(0.0f, 0.0f, 0.0f, sourceColor.a);
        }
    }

    return sourceColor;
}

#endif // EDGE_EFFECT_HLSLI

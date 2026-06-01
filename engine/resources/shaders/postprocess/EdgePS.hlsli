#ifndef EDGE_PS_HLSLI
#define EDGE_PS_HLSLI

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
        float luminance =
            CalcLuminance(sourceTexture.Sample(
                              sourceSampler, uv + kSobelOffsets[i] * texelSize)
                              .rgb);
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
        float depth =
            depthTexture.Sample(sourceSampler, uv + kSobelOffsets[i] * texelSize)
                .r;
        float viewZ = LinearizeDepth(depth);
        sobel.x += viewZ * kSobelX[i];
        sobel.y += viewZ * kSobelY[i];
    }

    return length(sobel);
}

float4 ApplyEdge(float4 sourceColor, Texture2D sourceTexture,
                 Texture2D depthTexture, SamplerState sourceSampler, float2 uv)
{
    if (edgeMode == 1 &&
        CalcLuminanceEdge(sourceTexture, sourceSampler, uv) >
            luminanceEdgeThreshold)
    {
        return float4(0.0f, 0.0f, 0.0f, sourceColor.a);
    }

    if (edgeMode == 2 &&
        CalcDepthEdge(depthTexture, sourceSampler, uv) > depthEdgeThreshold)
    {
        return float4(0.0f, 0.0f, 0.0f, sourceColor.a);
    }

    return sourceColor;
}

#endif // EDGE_PS_HLSLI

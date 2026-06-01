#ifndef TOON_PS_HLSLI
#define TOON_PS_HLSLI

float3 ApplyToon(Texture2D sourceTexture, SamplerState sourceSampler,
                 float3 color, float2 uv)
{
    if (toonEnabled == 0 || toonStrength <= 0.0001f)
    {
        return color;
    }

    float strength = saturate(toonStrength);
    float steps = max(toonColorSteps, 2.0f);
    float3 graded = floor(saturate(color) * steps + 0.5f) / steps;
    graded = saturate(graded * 1.06f);

    float center = CalcLuminance(sourceTexture.Sample(sourceSampler, uv).rgb);
    float l = CalcLuminance(sourceTexture.Sample(
                                sourceSampler, uv + float2(-texelSize.x, 0.0f))
                                .rgb);
    float r = CalcLuminance(sourceTexture.Sample(
                                sourceSampler, uv + float2(texelSize.x, 0.0f))
                                .rgb);
    float u = CalcLuminance(sourceTexture.Sample(
                                sourceSampler, uv + float2(0.0f, -texelSize.y))
                                .rgb);
    float d = CalcLuminance(sourceTexture.Sample(
                                sourceSampler, uv + float2(0.0f, texelSize.y))
                                .rgb);
    float edge = abs(center - l) + abs(center - r) + abs(center - u) + abs(center - d);
    edge = smoothstep(0.055f, 0.18f, edge) * saturate(toonEdgeStrength);

    float3 ink = float3(0.015f, 0.030f, 0.070f);
    float3 toonColor = lerp(graded, ink, edge);
    return lerp(color, toonColor, strength);
}

#endif // TOON_PS_HLSLI

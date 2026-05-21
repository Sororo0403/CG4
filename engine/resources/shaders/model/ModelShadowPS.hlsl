#include "Model.hlsli"

Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);

cbuffer Material : register(b2)
{
    float4 color;
    float4x4 uvTransform;
    int enableTexture;
    float reflectionStrength;
    float reflectionFresnelStrength;
    float reflectionRoughness;
    int blendMode;
    float alphaCutoff;
    int cullMode;
    int depthWrite;
    float roughness;
    float metallic;
    float normalStrength;
    int enableNormalMap;
};

void main(ModelVSOutput input)
{
    if (blendMode != 1)
    {
        return;
    }

    float2 uv = mul(float4(input.uv, 0.0f, 1.0f), uvTransform).xy;
    float alpha = color.a * input.color.a;
    if (enableTexture != 0)
    {
        alpha *= tex0.Sample(samp0, uv).a;
    }
    if (alpha < alphaCutoff)
    {
        discard;
    }
}

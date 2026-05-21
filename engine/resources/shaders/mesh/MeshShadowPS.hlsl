#include "Mesh.hlsli"

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
    float4 customParams;
};

void main(MeshVSOutput input)
{
    float2 uv = mul(float4(input.uv, 0.0f, 1.0f), uvTransform).xy;
    if (saturate(input.color.a) < Dither01(input.worldPos, uv))
    {
        discard;
    }

    if (blendMode != 1)
    {
        return;
    }

    float alpha = 1.0f;
    if (enableTexture != 0)
    {
        alpha = tex0.Sample(samp0, uv).a;
    }
    alpha *= color.a;
    if (alpha < alphaCutoff)
    {
        discard;
    }
}

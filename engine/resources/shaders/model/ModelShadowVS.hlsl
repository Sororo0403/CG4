#include "Model.hlsli"

cbuffer ObjectTransform : register(b0)
{
    float4x4 matWVP;
    float4x4 matWorld;
    float4x4 matWorldInverseTranspose;
};

ModelVSOutput main(ModelVSInput input)
{
    ModelVSOutput output;
    output.pos = mul(float4(input.pos, 1.0f), matWVP);
    output.uv = input.uv;
    output.worldPos = float3(0.0f, 0.0f, 0.0f);
    output.worldNormal = float3(0.0f, 1.0f, 0.0f);
    output.worldTangent = float4(1.0f, 0.0f, 0.0f, input.tangent.w);
    output.color = input.color;
    return output;
}

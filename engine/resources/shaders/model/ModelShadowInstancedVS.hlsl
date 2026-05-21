#include "Model.hlsli"

cbuffer ObjectTransform : register(b0)
{
    float4x4 matWVP;
    float4x4 matWorld;
    float4x4 matWorldInverseTranspose;
};

ModelVSOutput main(ModelInstanceInput input)
{
    ModelVSOutput output;
    float4x4 world = float4x4(input.world0, input.world1, input.world2, input.world3);
    float4 worldPos = mul(float4(input.pos, 1.0f), world);
    output.pos = mul(worldPos, matWVP);
    output.uv = input.uv;
    output.worldPos = worldPos.xyz;
    output.worldNormal = float3(0.0f, 1.0f, 0.0f);
    output.worldTangent = float4(1.0f, 0.0f, 0.0f, input.tangent.w);
    output.color = input.color * input.instanceColor;
    output.color.a *= input.fade;
    return output;
}

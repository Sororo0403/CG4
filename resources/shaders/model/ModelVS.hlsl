#include "Model.hlsli"

cbuffer ObjectTransform : register(b0)
{
    float4x4 matWVP;
    float4x4 matWorld;
};

struct Well
{
    float4x4 skeletonSpaceMatrix;
    float4x4 skeletonSpaceInverseTransposeMatrix;
};

StructuredBuffer<Well> gMatrixPalette : register(t1);

ModelVSOutput main(ModelVSInput input)
{
    ModelVSOutput o;

    float4 localPos = float4(input.pos, 1.0f);
    float3 localNormal = input.normal;

    float weightSum =
        input.weight.x +
        input.weight.y +
        input.weight.z +
        input.weight.w;

    float4 skinnedPos = localPos;
    float3 skinnedNormal = localNormal;

    if (weightSum > 0.0001f)
    {
        skinnedPos =
            mul(localPos, gMatrixPalette[input.index.x].skeletonSpaceMatrix) * input.weight.x +
            mul(localPos, gMatrixPalette[input.index.y].skeletonSpaceMatrix) * input.weight.y +
            mul(localPos, gMatrixPalette[input.index.z].skeletonSpaceMatrix) * input.weight.z +
            mul(localPos, gMatrixPalette[input.index.w].skeletonSpaceMatrix) * input.weight.w;

        skinnedNormal =
            mul(localNormal, (float3x3) gMatrixPalette[input.index.x].skeletonSpaceInverseTransposeMatrix) * input.weight.x +
            mul(localNormal, (float3x3) gMatrixPalette[input.index.y].skeletonSpaceInverseTransposeMatrix) * input.weight.y +
            mul(localNormal, (float3x3) gMatrixPalette[input.index.z].skeletonSpaceInverseTransposeMatrix) * input.weight.z +
            mul(localNormal, (float3x3) gMatrixPalette[input.index.w].skeletonSpaceInverseTransposeMatrix) * input.weight.w;
    }

    float4 worldPos = mul(skinnedPos, matWorld);
    float3 worldNormal = mul(skinnedNormal, (float3x3) matWorld);
    float normalLen = length(worldNormal);
    if (normalLen < 0.0001f)
    {
        worldNormal = float3(0.0f, 1.0f, 0.0f);
    }
    else
    {
        worldNormal /= normalLen;
    }

    o.pos = mul(skinnedPos, matWVP);
    o.uv = input.uv;
    o.worldPos = worldPos.xyz;
    o.worldNormal = worldNormal;
    return o;
}

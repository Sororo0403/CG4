#include "Model.hlsli"

cbuffer ObjectTransform : register(b0)
{
    float4x4 matWVP;
    float4x4 matWorld;
};

ModelVSOutput main(ModelVSInput input)
{
    ModelVSOutput o;

    float4 localPos = float4(input.pos, 1.0f);
    float3 localNormal = input.normal;

    float4 worldPos = mul(localPos, matWorld);
    float3 worldNormal = mul(localNormal, (float3x3) matWorld);
    float normalLen = length(worldNormal);
    if (normalLen < 0.0001f)
    {
        worldNormal = float3(0.0f, 1.0f, 0.0f);
    }
    else
    {
        worldNormal /= normalLen;
    }

    o.pos = mul(localPos, matWVP);
    o.uv = input.uv;
    o.worldPos = worldPos.xyz;
    o.worldNormal = worldNormal;
    return o;
}

#include "Mesh.hlsli"

cbuffer ObjectTransform : register(b0)
{
    float4x4 matWVP;
    float4x4 matWorld;
    float4x4 matWorldInverseTranspose;
};

cbuffer SceneParams : register(b1)
{
    float4 cameraPos;
    float4 keyLightDirection;
    float4 keyLightColor;
    float4 fillLightDirection;
    float4 fillLightColor;
    float4 ambientColor;
    PointLight pointLights[2];
    float4 lightingParams;
    float4 fogColor;
    float4 fogParams;
    float4x4 viewProjection;
    float4x4 lightViewProjection;
    float4 shadowParams;
    float4 shadowFilterParams;
    float4 customSceneParams0;
    float4 customSceneParams1;
};

MeshVSOutput main(MeshVSInput input)
{
    MeshVSOutput output;

    float4 localPos = float4(input.pos, 1.0f);
    float4 worldPos = mul(localPos, matWorld);
    float3 worldNormal = mul(input.normal, (float3x3) matWorldInverseTranspose);
    float normalLength = length(worldNormal);
    worldNormal = normalLength > 0.0001f ? worldNormal / normalLength : float3(0.0f, 1.0f, 0.0f);
    float3 worldTangent = mul(input.tangent.xyz, (float3x3) matWorld);
    float tangentLength = length(worldTangent);
    worldTangent = tangentLength > 0.0001f ? worldTangent / tangentLength : float3(1.0f, 0.0f, 0.0f);
    output.pos = mul(worldPos, viewProjection);
    output.uv = input.uv;
    output.worldPos = worldPos.xyz;
    output.worldNormal = worldNormal;
    output.worldTangent = float4(worldTangent, input.tangent.w);
    output.custom0 = input.custom0;
    output.color = input.color;
    return output;
}

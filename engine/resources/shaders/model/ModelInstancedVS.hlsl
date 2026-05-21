#include "Model.hlsli"

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
    float4 lightingModeParams;
    float4 fogColor;
    float4 fogParams;
    float4x4 viewProjection;
    float4x4 lightViewProjection;
    float4 shadowParams;
    float4 shadowFilterParams;
};

ModelVSOutput main(ModelInstanceInput input)
{
    ModelVSOutput output;

    float4x4 world = float4x4(input.world0, input.world1, input.world2, input.world3);
    float4 localPos = float4(input.pos, 1.0f);
    float4 worldPos = mul(localPos, world);

    float3 worldNormal = mul(input.normal, (float3x3) world);
    float normalLength = length(worldNormal);
    worldNormal = normalLength > 0.0001f ? worldNormal / normalLength : float3(0.0f, 1.0f, 0.0f);

    output.pos = mul(worldPos, viewProjection);
    output.uv = input.uv;
    output.worldPos = worldPos.xyz;
    output.worldNormal = worldNormal;
    float3 worldTangent = mul(input.tangent.xyz, (float3x3) world);
    float tangentLength = length(worldTangent);
    output.worldTangent = float4(tangentLength > 0.0001f ? worldTangent / tangentLength : float3(1.0f, 0.0f, 0.0f), input.tangent.w);
    output.color = input.color * input.instanceColor;
    output.color.a *= input.fade;
    return output;
}

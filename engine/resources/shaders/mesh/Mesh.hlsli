#ifndef MESH_HLSLI
#define MESH_HLSLI

#include "../common/LightingTypes.hlsli"

struct MeshVSInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
    float4 tangent : TANGENT;
};

struct MeshInstanceInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
    float4 tangent : TANGENT;
    float4 world0 : WORLD0;
    float4 world1 : WORLD1;
    float4 world2 : WORLD2;
    float4 world3 : WORLD3;
    float4 instanceColor : INSTANCECOLOR;
    float fade : INSTANCEFADE;
    uint customId : INSTANCECUSTOMID;
};

struct MeshVSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 worldPos : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
    float4 worldTangent : TEXCOORD3;
    float surfaceWeight : TEXCOORD4;
    float3 ditherPos : TEXCOORD5;
    nointerpolation float coverageHash : TEXCOORD6;
    float4 surfaceParams : TEXCOORD7;
    float4 color : COLOR;
};

struct MeshWorldTransform
{
    float4 position;
    float3 normal;
    float3 tangent;
};

float3 MeshNormalizeOrDefault(float3 value, float3 fallback)
{
    const float lengthValue = length(value);
    return lengthValue > 0.0001f ? value / lengthValue : fallback;
}

MeshWorldTransform BuildMeshWorldTransform(
    MeshVSInput input, float4x4 world, float4x4 worldInverseTranspose)
{
    MeshWorldTransform result;
    result.position = mul(float4(input.pos, 1.0f), world);
    result.normal = MeshNormalizeOrDefault(
        mul(input.normal, (float3x3) worldInverseTranspose),
        float3(0.0f, 1.0f, 0.0f));
    result.tangent = MeshNormalizeOrDefault(
        mul(input.tangent.xyz, (float3x3) world),
        float3(1.0f, 0.0f, 0.0f));
    return result;
}

float4x4 BuildMeshInstanceWorldMatrix(MeshInstanceInput input)
{
    return float4x4(input.world0, input.world1, input.world2, input.world3);
}

MeshWorldTransform BuildMeshInstanceWorldTransformFromMatrix(
    MeshInstanceInput input, float4x4 world)
{
    MeshWorldTransform result;
    result.position = mul(float4(input.pos, 1.0f), world);
    result.normal = MeshNormalizeOrDefault(
        mul(input.normal, (float3x3) world),
        float3(0.0f, 1.0f, 0.0f));
    result.tangent = MeshNormalizeOrDefault(
        mul(input.tangent.xyz, (float3x3) world),
        float3(1.0f, 0.0f, 0.0f));
    return result;
}

MeshWorldTransform BuildMeshInstanceWorldTransform(MeshInstanceInput input)
{
    return BuildMeshInstanceWorldTransformFromMatrix(
        input, BuildMeshInstanceWorldMatrix(input));
}

float Dither01(float3 worldPos, float2 uv)
{
    float value = dot(worldPos.xz + uv * 3.17f, float2(12.9898f, 78.233f)) +
                  worldPos.y * 37.719f;
    return frac(sin(value) * 43758.5453f);
}

MeshVSOutput BuildMeshVertexOutput(
    MeshWorldTransform worldTransform,
    float4x4 clipTransform,
    float2 uv,
    float tangentW,
    float surfaceWeight,
    float4 surfaceParams,
    float4 color)
{
    MeshVSOutput output;
    output.pos = mul(worldTransform.position, clipTransform);
    output.uv = uv;
    output.worldPos = worldTransform.position.xyz;
    output.worldNormal = worldTransform.normal;
    output.worldTangent = float4(worldTransform.tangent, tangentW);
    output.surfaceWeight = surfaceWeight;
    output.ditherPos = worldTransform.position.xyz;
    output.coverageHash = Dither01(worldTransform.position.xyz, uv);
    output.surfaceParams = surfaceParams;
    output.color = color;
    return output;
}

#endif // MESH_HLSLI

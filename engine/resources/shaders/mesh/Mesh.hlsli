#ifndef MESH_HLSLI
#define MESH_HLSLI

struct MeshVSInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
    float4 tangent : TANGENT;
    float custom0 : CUSTOM0;
};

struct MeshInstanceInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
    float4 tangent : TANGENT;
    float custom0 : CUSTOM0;
    float4 world0 : WORLD0;
    float4 world1 : WORLD1;
    float4 world2 : WORLD2;
    float4 world3 : WORLD3;
    float4 instanceColor : INSTANCECOLOR;
    float fade : INSTANCEFADE;
    uint seed : INSTANCESEED;
};

struct MeshVSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 worldPos : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
    float4 worldTangent : TEXCOORD3;
    float custom0 : TEXCOORD4;
    float4 color : COLOR;
};

struct PointLight
{
    float4 positionRange;
    float4 colorIntensity;
};

float Dither01(float3 worldPos, float2 uv)
{
    float value = dot(worldPos.xz + uv * 3.17f, float2(12.9898f, 78.233f)) +
                  worldPos.y * 37.719f;
    return frac(sin(value) * 43758.5453f);
}

#endif // MESH_HLSLI

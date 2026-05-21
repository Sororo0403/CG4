#ifndef MODEL_HLSLI
#define MODEL_HLSLI

struct ModelVSInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
    float4 tangent : TANGENT;
};

struct ModelInstanceInput
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
    uint seed : INSTANCESEED;
};

struct ModelVSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 worldPos : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
    float4 worldTangent : TEXCOORD3;
    float4 color : COLOR;
};

struct PointLight
{
    float4 positionRange;
    float4 colorIntensity;
};

#endif // MODEL_HLSLI

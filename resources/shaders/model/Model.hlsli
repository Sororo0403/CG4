#ifndef MODEL_HLSLI
#define MODEL_HLSLI

struct ModelVSInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 weight : WEIGHT;
    int4 index : INDEX;
};

struct ModelVSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 worldPos : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
};

struct PointLight
{
    float4 positionRange;
    float4 colorIntensity;
};

#endif // MODEL_HLSLI

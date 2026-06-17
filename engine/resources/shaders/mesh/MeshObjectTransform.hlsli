#ifndef MESH_OBJECT_TRANSFORM_HLSLI
#define MESH_OBJECT_TRANSFORM_HLSLI

cbuffer ObjectTransform : register(b0)
{
    float4x4 matWVP;
    float4x4 matWorld;
    float4x4 matWorldInverseTranspose;
};

#endif // MESH_OBJECT_TRANSFORM_HLSLI

#ifndef MESH_MATERIAL_PARAMS_HLSLI
#define MESH_MATERIAL_PARAMS_HLSLI

cbuffer Material : register(b2)
{
    float4 color;
    float4x4 uvTransform;
    int enableTexture;
    float reflectionStrength;
    float reflectionFresnelStrength;
    float reflectionRoughness;
    int blendMode;
    float alphaCutoff;
    int cullMode;
    int depthWrite;
    float roughness;
    float metallic;
    float normalStrength;
    int enableNormalMap;
    float4 customParams;
    float4 customParams2;
    float4 customParams3;
};

#endif // MESH_MATERIAL_PARAMS_HLSLI

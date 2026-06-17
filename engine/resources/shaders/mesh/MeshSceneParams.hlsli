#ifndef MESH_SCENE_PARAMS_HLSLI
#define MESH_SCENE_PARAMS_HLSLI

#include "../common/LightingTypes.hlsli"

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

#endif // MESH_SCENE_PARAMS_HLSLI

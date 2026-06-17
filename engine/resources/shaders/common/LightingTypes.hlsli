#ifndef LIGHTING_TYPES_HLSLI
#define LIGHTING_TYPES_HLSLI

struct PointLight
{
    float4 positionRange;
    float4 colorIntensity;
};

struct SpotLight
{
    float4 positionRange;
    float4 direction;
    float4 colorIntensity;
    float4 angleParams;
};

#endif // LIGHTING_TYPES_HLSLI

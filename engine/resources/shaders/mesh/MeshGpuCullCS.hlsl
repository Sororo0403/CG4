struct InstanceData
{
    float4 world0;
    float4 world1;
    float4 world2;
    float4 world3;
    float4 color;
    float fade;
    uint customId;
    float2 padding;
};

cbuffer MeshGpuCullParams : register(b0)
{
    float4 frustumPlanes[6];
    float4 cameraAndMaxDistanceSq;
    float4 localCenterAndRadius;
    float4x4 occlusionViewProjection;
    float4 occlusionParams;
    uint instanceCount;
    uint enableDistanceCull;
    float minDistanceSq;
    uint enableMinDistanceCull;
};

StructuredBuffer<InstanceData> gInputInstances : register(t0);
Texture2D<float> gOcclusionPyramid : register(t1);
RWStructuredBuffer<InstanceData> gOutputInstances : register(u0);
RWByteAddressBuffer gVisibleCount : register(u1);

float2 ClipToUv(float4 clip)
{
    float2 ndc = clip.xy / max(clip.w, 0.0001f);
    return ndc * float2(0.5f, -0.5f) + 0.5f;
}

bool IsOccludedByDepthPyramid(float3 center, float radius)
{
    if (occlusionParams.z < 0.5f || occlusionParams.x < 1.0f ||
        occlusionParams.y < 1.0f)
    {
        return false;
    }

    float3 cameraToCenter = center - cameraAndMaxDistanceSq.xyz;
    float cameraDistance = length(cameraToCenter);
    float nearOcclusionGuard = max(radius * 4.0f, 6.0f);
    if (cameraDistance <= nearOcclusionGuard)
    {
        return false;
    }

    float3 axes[3] = {
        float3(radius, 0.0f, 0.0f),
        float3(0.0f, radius, 0.0f),
        float3(0.0f, 0.0f, radius),
    };

    float4 centerClip = mul(float4(center, 1.0f), occlusionViewProjection);
    if (centerClip.w <= 0.0001f)
    {
        return false;
    }

    float2 centerUv = ClipToUv(centerClip);
    if (any(centerUv < 0.0f) || any(centerUv > 1.0f))
    {
        return false;
    }

    float2 minUv = centerUv;
    float2 maxUv = centerUv;
    float minDepth = centerClip.z / max(centerClip.w, 0.0001f);

    float3 viewDir = cameraToCenter / max(cameraDistance, 0.0001f);
    float4 frontClip =
        mul(float4(center - viewDir * radius, 1.0f), occlusionViewProjection);
    if (frontClip.w <= 0.0001f)
    {
        return false;
    }
    float2 frontUv = ClipToUv(frontClip);
    if (any(frontUv < 0.0f) || any(frontUv > 1.0f))
    {
        return false;
    }
    minUv = min(minUv, frontUv);
    maxUv = max(maxUv, frontUv);
    minDepth = min(minDepth, frontClip.z / max(frontClip.w, 0.0001f));

    [unroll]
    for (uint axisIndex = 0u; axisIndex < 3u; ++axisIndex)
    {
        [unroll]
        for (uint side = 0u; side < 2u; ++side)
        {
            float3 spherePoint =
                center + axes[axisIndex] * (side == 0u ? -1.0f : 1.0f);
            float4 clip = mul(float4(spherePoint, 1.0f), occlusionViewProjection);
            if (clip.w <= 0.0001f)
            {
                return false;
            }
            float2 uv = ClipToUv(clip);
            if (any(uv < 0.0f) || any(uv > 1.0f))
            {
                return false;
            }
            minUv = min(minUv, uv);
            maxUv = max(maxUv, uv);
            minDepth = min(minDepth, clip.z / max(clip.w, 0.0001f));
        }
    }

    float2 uvPadding = max(2.0f / occlusionParams.xy, (maxUv - minUv) * 0.08f);
    minUv = saturate(minUv - uvPadding);
    maxUv = saturate(maxUv + uvPadding);

    if (minDepth <= 0.0f || minDepth >= 1.0f)
    {
        return false;
    }

    float2 sizePixels = max((maxUv - minUv) * occlusionParams.xy, 1.0f);
    float mip = clamp(ceil(log2(max(sizePixels.x, sizePixels.y))) - 1.0f,
                      0.0f, occlusionParams.z - 1.0f);
    uint mipIndex = (uint)mip;
    uint2 baseSize = max((uint2)occlusionParams.xy, uint2(1u, 1u));
    uint2 mipSize = max(baseSize / (1u << mipIndex), uint2(1u, 1u));
    uint2 p0 = min((uint2)(minUv * (float2)mipSize), mipSize - 1u);
    uint2 p1 = min((uint2)(maxUv * (float2)mipSize), mipSize - 1u);

    float maxDepth = 0.0f;
    maxDepth = max(maxDepth, gOcclusionPyramid.Load(int3(p0, mipIndex)));
    maxDepth = max(maxDepth, gOcclusionPyramid.Load(int3(uint2(p1.x, p0.y), mipIndex)));
    maxDepth = max(maxDepth, gOcclusionPyramid.Load(int3(uint2(p0.x, p1.y), mipIndex)));
    maxDepth = max(maxDepth, gOcclusionPyramid.Load(int3(p1, mipIndex)));

    return maxDepth + occlusionParams.w < minDepth;
}

bool IsVisible(InstanceData instance)
{
    float3 center = localCenterAndRadius.x * instance.world0.xyz +
                    localCenterAndRadius.y * instance.world1.xyz +
                    localCenterAndRadius.z * instance.world2.xyz +
                    instance.world3.xyz;
    float3 worldX = instance.world0.xyz;
    float3 worldY = instance.world1.xyz;
    float3 worldZ = instance.world2.xyz;
    float scale = max(length(worldX), max(length(worldY), length(worldZ)));
    float radius = max(localCenterAndRadius.w * max(scale, 0.0001f), 0.0001f);

    if (enableDistanceCull != 0u)
    {
        float2 delta = center.xz - cameraAndMaxDistanceSq.xz;
        float distanceLimit = sqrt(max(cameraAndMaxDistanceSq.w, 0.0f)) + radius;
        if (dot(delta, delta) > distanceLimit * distanceLimit)
        {
            return false;
        }
    }
    if (enableMinDistanceCull != 0u)
    {
        float2 delta = center.xz - cameraAndMaxDistanceSq.xz;
        float distanceLimit = max(sqrt(max(minDistanceSq, 0.0f)) - radius, 0.0f);
        if (dot(delta, delta) < distanceLimit * distanceLimit)
        {
            return false;
        }
    }

    [unroll]
    for (uint planeIndex = 0u; planeIndex < 6u; ++planeIndex)
    {
        float4 plane = frustumPlanes[planeIndex];
        if (dot(plane.xyz, center) + plane.w < -radius)
        {
            return false;
        }
    }

    if (IsOccludedByDepthPyramid(center, radius))
    {
        return false;
    }

    return true;
}

[numthreads(128, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint sourceIndex = dispatchThreadId.x;
    if (sourceIndex >= instanceCount)
    {
        return;
    }

    InstanceData instance = gInputInstances[sourceIndex];
    if (!IsVisible(instance))
    {
        return;
    }

    uint outputIndex = 0u;
    gVisibleCount.InterlockedAdd(0, 1u, outputIndex);
    if (outputIndex < instanceCount)
    {
        gOutputInstances[outputIndex] = instance;
    }
}

struct RayConstants
{
    float4x4 inverseViewProjection;
    float4 cameraPositionTime;
    float4 outputSize;
    float4 sunDirectionIntensity;
    float4 anchorRadius;
    float4 mirrorOriginIntensity;
    float4 mirrorRight;
    float4 mirrorUp;
    float4 mirrorNormal;
};

struct RayPayload
{
    float3 color;
    float hitT;
};

RaytracingAccelerationStructure gScene : register(t0);
RWTexture2D<float4> gOutput : register(u0);
ConstantBuffer<RayConstants> gConstants : register(b0);

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float3 SkyColor(float3 rayDir)
{
    const float up = saturate(rayDir.y * 0.5f + 0.5f);
    const float sun = pow(saturate(dot(rayDir, normalize(gConstants.sunDirectionIntensity.xyz))), 32.0f);
    return lerp(float3(0.045f, 0.085f, 0.14f), float3(0.28f, 0.48f, 0.76f), up) +
           float3(1.0f, 0.72f, 0.36f) * sun * 0.75f;
}

[shader("raygeneration")]
void AdvancedGpuRayGen()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 dims = DispatchRaysDimensions().xy;
    const float2 uv = (float2(pixel) + 0.5f) / max(float2(dims), 1.0f);
    const float2 jitter = (Hash12(float2(pixel) + gConstants.cameraPositionTime.w) - 0.5f) *
                          gConstants.outputSize.zw;
    const float2 screenUv = saturate(uv + jitter * 0.35f);
    const float2 ndc = screenUv * 2.0f - 1.0f;
    const float4 clip = float4(ndc.x, -ndc.y, 1.0f, 1.0f);
    const float4 farWorld = mul(clip, gConstants.inverseViewProjection);
    const float3 worldPoint = farWorld.xyz / max(abs(farWorld.w), 0.00001f);

    RayDesc ray;
    ray.Origin = gConstants.cameraPositionTime.xyz;
    ray.Direction = normalize(worldPoint - ray.Origin);
    ray.TMin = 0.05f;
    ray.TMax = 90.0f;

    RayPayload payload;
    payload.color = SkyColor(ray.Direction);
    payload.hitT = -1.0f;

    TraceRay(gScene, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 1, 0, ray, payload);

    const float panelFade = smoothstep(90.0f, 2.0f, payload.hitT);
    const float alpha = payload.hitT >= 0.0f ? 0.78f : 0.34f;
    const float vignette = smoothstep(0.88f, 0.20f, length(uv * 2.0f - 1.0f));
    gOutput[pixel] = float4(payload.color * (0.62f + panelFade * 0.38f),
                            alpha * vignette);
}

[shader("raygeneration")]
void AdvancedGpuMirrorRayGen()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 dims = DispatchRaysDimensions().xy;
    const float2 uv = (float2(pixel) + 0.5f) / max(float2(dims), 1.0f);
    const float jitterSeed = Hash12(float2(pixel) + gConstants.cameraPositionTime.w * 7.31f);
    const float2 jitter = (float2(jitterSeed, Hash12(float2(pixel.yx) + jitterSeed)) - 0.5f) *
                          gConstants.outputSize.zw * 0.30f;
    const float2 mirrorUv = saturate(uv + jitter);
    const float3 mirrorPoint = gConstants.mirrorOriginIntensity.xyz +
                               gConstants.mirrorRight.xyz * mirrorUv.x +
                               gConstants.mirrorUp.xyz * (1.0f - mirrorUv.y);

    float3 normal = normalize(gConstants.mirrorNormal.xyz);
    float3 viewIncident = normalize(mirrorPoint - gConstants.cameraPositionTime.xyz);
    if (dot(-viewIncident, normal) < 0.0f)
    {
        normal = -normal;
    }

    RayDesc ray;
    ray.Origin = mirrorPoint + normal * 0.035f;
    ray.Direction = normalize(reflect(viewIncident, normal));
    ray.TMin = 0.05f;
    ray.TMax = 90.0f;

    RayPayload payload;
    payload.color = SkyColor(ray.Direction);
    payload.hitT = -1.0f;

    TraceRay(gScene, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 1, 0, ray, payload);

    const float edge = max(abs(uv.x - 0.5f), abs(uv.y - 0.5f)) * 2.0f;
    const float edgeFade = smoothstep(1.0f, 0.82f, edge);
    const float hitLift = payload.hitT >= 0.0f ? 1.0f : 0.84f;
    const float3 tint = lerp(float3(0.72f, 0.86f, 0.98f), float3(1.0f, 1.0f, 1.0f),
                             saturate(gConstants.mirrorOriginIntensity.w));
    gOutput[pixel] = float4(payload.color * tint * hitLift,
                            saturate(0.88f * edgeFade + 0.08f));
}

[shader("miss")]
void AdvancedGpuMiss(inout RayPayload payload)
{
    payload.color = lerp(payload.color, float3(0.12f, 0.22f, 0.33f), 0.22f);
    payload.hitT = -1.0f;
}

[shader("closesthit")]
void AdvancedGpuClosestHit(inout RayPayload payload,
                           BuiltInTriangleIntersectionAttributes attributes)
{
    const uint instanceId = InstanceID();
    const float3 bary = float3(1.0f - attributes.barycentrics.x -
                                   attributes.barycentrics.y,
                               attributes.barycentrics.x,
                               attributes.barycentrics.y);
    const float edge = 1.0f - smoothstep(0.0f, 0.055f, min(bary.x, min(bary.y, bary.z)));
    const float shade = saturate(dot(WorldRayDirection(), normalize(gConstants.sunDirectionIntensity.xyz)) *
                                 -0.45f + 0.72f);
    float3 baseColor = float3(((instanceId >> 16u) & 255u) / 255.0f,
                              ((instanceId >> 8u) & 255u) / 255.0f,
                              (instanceId & 255u) / 255.0f);
    if (instanceId == 0u)
    {
        baseColor = float3(0.42f, 0.48f, 0.54f);
    }

    const float distanceFade = saturate(1.0f - RayTCurrent() / 90.0f);
    const float edgeDebugStrength =
        gConstants.mirrorOriginIntensity.w > 0.0f ? 0.08f : 1.0f;
    payload.color = baseColor * (0.24f + shade * 0.86f) +
                    edge * float3(0.18f, 0.55f, 0.95f) * edgeDebugStrength +
                    distanceFade * 0.04f;
    payload.hitT = RayTCurrent();
}

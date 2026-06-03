cbuffer MeshGpuCullArgsParams : register(b0)
{
    uint indexCountPerInstance;
    uint maxInstanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
    uint3 paddingParams;
};

RWByteAddressBuffer gVisibleCount : register(u1);
RWByteAddressBuffer gDrawArgs : register(u2);

[numthreads(1, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x != 0u)
    {
        return;
    }

    uint visibleCount = min(gVisibleCount.Load(0), maxInstanceCount);
    gDrawArgs.Store(0, indexCountPerInstance);
    gDrawArgs.Store(4, visibleCount);
    gDrawArgs.Store(8, startIndexLocation);
    gDrawArgs.Store(12, asuint(baseVertexLocation));
    gDrawArgs.Store(16, startInstanceLocation);
}

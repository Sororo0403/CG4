cbuffer MeshGpuLodCullArgsParams : register(b0)
{
    uint4 indexCountPerInstance;
    uint maxInstanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
};

RWByteAddressBuffer gCountLod0 : register(u3);
RWByteAddressBuffer gCountLod1 : register(u4);
RWByteAddressBuffer gCountLod2 : register(u5);
RWByteAddressBuffer gDrawArgsLod0 : register(u6);
RWByteAddressBuffer gDrawArgsLod1 : register(u7);
RWByteAddressBuffer gDrawArgsLod2 : register(u8);

[numthreads(1, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x != 0u)
    {
        return;
    }

    uint count0 = min(gCountLod0.Load(0), maxInstanceCount);
    uint count1 = min(gCountLod1.Load(0), maxInstanceCount);
    uint count2 = min(gCountLod2.Load(0), maxInstanceCount);

    gDrawArgsLod0.Store(0, indexCountPerInstance.x);
    gDrawArgsLod0.Store(4, count0);
    gDrawArgsLod0.Store(8, startIndexLocation);
    gDrawArgsLod0.Store(12, asuint(baseVertexLocation));
    gDrawArgsLod0.Store(16, startInstanceLocation);

    gDrawArgsLod1.Store(0, indexCountPerInstance.y);
    gDrawArgsLod1.Store(4, count1);
    gDrawArgsLod1.Store(8, startIndexLocation);
    gDrawArgsLod1.Store(12, asuint(baseVertexLocation));
    gDrawArgsLod1.Store(16, startInstanceLocation);

    gDrawArgsLod2.Store(0, indexCountPerInstance.z);
    gDrawArgsLod2.Store(4, count2);
    gDrawArgsLod2.Store(8, startIndexLocation);
    gDrawArgsLod2.Store(12, asuint(baseVertexLocation));
    gDrawArgsLod2.Store(16, startInstanceLocation);
}

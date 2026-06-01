#define MAX_PARTICLE_ARGS_JOBS 16

cbuffer ParticleArgsParams : register(b0)
{
    uint jobCount;
    uint maxInstanceCount0;
    uint maxInstanceCount1;
    uint maxInstanceCount2;
    uint maxInstanceCount3;
    uint maxInstanceCount4;
    uint maxInstanceCount5;
    uint maxInstanceCount6;
    uint maxInstanceCount7;
    uint maxInstanceCount8;
    uint maxInstanceCount9;
    uint maxInstanceCount10;
    uint maxInstanceCount11;
    uint maxInstanceCount12;
    uint maxInstanceCount13;
    uint maxInstanceCount14;
    uint maxInstanceCount15;
};

RWByteAddressBuffer gActiveCount : register(u0);
RWByteAddressBuffer gDrawArgsBuffer : register(u32);

uint GetMaxInstanceCount(uint jobIndex)
{
    if (jobIndex == 0u) return maxInstanceCount0;
    if (jobIndex == 1u) return maxInstanceCount1;
    if (jobIndex == 2u) return maxInstanceCount2;
    if (jobIndex == 3u) return maxInstanceCount3;
    if (jobIndex == 4u) return maxInstanceCount4;
    if (jobIndex == 5u) return maxInstanceCount5;
    if (jobIndex == 6u) return maxInstanceCount6;
    if (jobIndex == 7u) return maxInstanceCount7;
    if (jobIndex == 8u) return maxInstanceCount8;
    if (jobIndex == 9u) return maxInstanceCount9;
    if (jobIndex == 10u) return maxInstanceCount10;
    if (jobIndex == 11u) return maxInstanceCount11;
    if (jobIndex == 12u) return maxInstanceCount12;
    if (jobIndex == 13u) return maxInstanceCount13;
    if (jobIndex == 14u) return maxInstanceCount14;
    return maxInstanceCount15;
}

[numthreads(32, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint jobIndex = dispatchThreadId.x;
    if (jobIndex >= jobCount)
    {
        return;
    }

    uint activeCount =
        min(gActiveCount.Load(0), GetMaxInstanceCount(jobIndex));
    gDrawArgsBuffer.Store(0, 6u);
    gDrawArgsBuffer.Store(4, activeCount);
    gDrawArgsBuffer.Store(8, 0u);
    gDrawArgsBuffer.Store(12, 0u);
}

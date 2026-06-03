cbuffer DepthPyramidConstants : register(b0)
{
    uint sourceWidth;
    uint sourceHeight;
    uint targetWidth;
    uint targetHeight;
    uint sourceMip;
    uint3 padding;
};

Texture2D<float> gSourceDepth : register(t0);
RWTexture2D<float> gTargetMip : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 dst = dispatchThreadId.xy;
    if (dst.x >= targetWidth || dst.y >= targetHeight)
    {
        return;
    }

    uint2 srcBase = dst * 2u;
    float maxDepth = 0.0f;

    [unroll]
    for (uint y = 0u; y < 2u; ++y)
    {
        [unroll]
        for (uint x = 0u; x < 2u; ++x)
        {
            uint2 src = min(srcBase + uint2(x, y),
                            uint2(sourceWidth - 1u, sourceHeight - 1u));
            maxDepth = max(maxDepth, gSourceDepth.Load(int3(src, sourceMip)));
        }
    }

    gTargetMip[dst] = maxDepth;
}

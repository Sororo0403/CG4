#include "PostEffect.hlsli"

PostEffectVSOutput main(uint vertexId : SV_VertexID)
{
    float2 positions[3] = {
        float2(-1.0f, 3.0f),
        float2(-1.0f, -1.0f),
        float2(3.0f, -1.0f),
    };

    float2 uvs[3] = {
        float2(0.0f, -1.0f),
        float2(0.0f, 1.0f),
        float2(2.0f, 1.0f),
    };

    PostEffectVSOutput output;
    output.pos = float4(positions[vertexId], 0.0f, 1.0f);
    output.uv = uvs[vertexId];
    return output;
}

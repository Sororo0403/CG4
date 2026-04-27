struct Vertex
{
    float3 pos;
    float3 normal;
    float2 uv;
};

struct VertexInfluence
{
    float4 weight;
    int4 index;
};

struct Well
{
    float4x4 skeletonSpaceMatrix;
    float4x4 skeletonSpaceInverseTransposeMatrix;
};

cbuffer SkinningInformation : register(b0)
{
    uint vertexCount;
    uint3 padding;
};

StructuredBuffer<Vertex> gInputVertices : register(t0);
StructuredBuffer<VertexInfluence> gInfluences : register(t1);
StructuredBuffer<Well> gMatrixPalette : register(t2);
RWStructuredBuffer<Vertex> gOutputVertices : register(u0);

[numthreads(1024, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint vertexIndex = dispatchThreadId.x;
    if (vertexIndex >= vertexCount)
    {
        return;
    }

    const Vertex input = gInputVertices[vertexIndex];
    const VertexInfluence influence = gInfluences[vertexIndex];

    const float weightSum =
        influence.weight.x +
        influence.weight.y +
        influence.weight.z +
        influence.weight.w;

    Vertex output = input;

    if (weightSum > 0.0001f)
    {
        const float4 localPos = float4(input.pos, 1.0f);
        output.pos =
            (mul(localPos, gMatrixPalette[influence.index.x].skeletonSpaceMatrix) * influence.weight.x +
             mul(localPos, gMatrixPalette[influence.index.y].skeletonSpaceMatrix) * influence.weight.y +
             mul(localPos, gMatrixPalette[influence.index.z].skeletonSpaceMatrix) * influence.weight.z +
             mul(localPos, gMatrixPalette[influence.index.w].skeletonSpaceMatrix) * influence.weight.w)
                .xyz;

        output.normal =
            mul(input.normal, (float3x3) gMatrixPalette[influence.index.x].skeletonSpaceInverseTransposeMatrix) * influence.weight.x +
            mul(input.normal, (float3x3) gMatrixPalette[influence.index.y].skeletonSpaceInverseTransposeMatrix) * influence.weight.y +
            mul(input.normal, (float3x3) gMatrixPalette[influence.index.z].skeletonSpaceInverseTransposeMatrix) * influence.weight.z +
            mul(input.normal, (float3x3) gMatrixPalette[influence.index.w].skeletonSpaceInverseTransposeMatrix) * influence.weight.w;

        const float normalLength = length(output.normal);
        output.normal = normalLength > 0.0001f ? output.normal / normalLength : float3(0.0f, 1.0f, 0.0f);
    }

    gOutputVertices[vertexIndex] = output;
}

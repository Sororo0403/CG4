#ifndef MESH_STANDARD_VS_MAIN_HLSLI
#define MESH_STANDARD_VS_MAIN_HLSLI

#include "Mesh.hlsli"
#include "MeshObjectTransform.hlsli"
#include "MeshSceneParams.hlsli"

MeshVSOutput main(MeshVSInput input)
{
    MeshWorldTransform worldTransform =
        BuildMeshWorldTransform(input, matWorld, matWorldInverseTranspose);
    return BuildMeshVertexOutput(
        worldTransform,
        viewProjection,
        input.uv,
        input.tangent.w,
        0.0f,
        float4(0.0f, 0.0f, 0.0f, 0.0f),
        input.color);
}

#endif // MESH_STANDARD_VS_MAIN_HLSLI

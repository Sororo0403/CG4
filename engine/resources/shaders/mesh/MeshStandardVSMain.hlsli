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
        input.customScalar0,
        input.customVector0,
        input.color);
}

#endif // MESH_STANDARD_VS_MAIN_HLSLI

#ifndef MESH_INSTANCED_VS_MAIN_HLSLI
#define MESH_INSTANCED_VS_MAIN_HLSLI

#include "Mesh.hlsli"
#include "MeshObjectTransform.hlsli"
#include "MeshSceneParams.hlsli"

MeshVSOutput main(MeshInstanceInput input)
{
    MeshWorldTransform worldTransform = BuildMeshInstanceWorldTransform(input);
    float4 color = input.color * input.instanceColor;
    color.a *= input.fade;
    return BuildMeshVertexOutput(
        worldTransform,
        viewProjection,
        input.uv,
        input.tangent.w,
        0.0f,
        float4(0.0f, 0.0f, 0.0f, 0.0f),
        color);
}

#endif // MESH_INSTANCED_VS_MAIN_HLSLI

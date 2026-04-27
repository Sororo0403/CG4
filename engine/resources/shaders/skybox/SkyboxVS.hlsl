#include "Skybox.hlsli"

VertexShaderOutput main(VertexShaderInput input) {
    VertexShaderOutput output;

    float4 position = mul(float4(input.position, 1.0f), gWVP);
    output.position = position.xyww;
    output.texcoord = input.position;

    return output;
}

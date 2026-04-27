struct VertexShaderInput {
    float3 position : POSITION0;
};

struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float3 texcoord : TEXCOORD0;
};

cbuffer gTransform : register(b0) {
    float4x4 gWVP;
}

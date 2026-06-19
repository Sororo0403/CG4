#include "graphics/DxUtils.h"

#include "graphics/DxHelpers.h"

UINT DxUtils::Align256(UINT size) {
    return (size + 0xFF) & ~0xFF;
}

void DxUtils::ConfigureViewportAndScissor(UINT width, UINT height, D3D12_VIEWPORT& viewport,
                                          D3D12_RECT& scissorRect) {
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(width);
    scissorRect.bottom = static_cast<LONG>(height);
}

void DxUtils::ConfigureAlphaBlend(D3D12_RENDER_TARGET_BLEND_DESC& renderTarget) {
    renderTarget.BlendEnable = TRUE;
    renderTarget.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    renderTarget.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
    renderTarget.SrcBlendAlpha = D3D12_BLEND_ONE;
    renderTarget.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    renderTarget.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    renderTarget.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
}

void DxUtils::ConfigureFullscreenTrianglePipeline(D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
                                                  DXGI_FORMAT renderTargetFormat,
                                                  DXGI_FORMAT depthStencilFormat) {
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = renderTargetFormat;
    desc.DSVFormat = depthStencilFormat;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState = depth;
}

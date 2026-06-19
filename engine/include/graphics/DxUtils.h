#pragma once
#include <Windows.h>
#include <d3d12.h>

namespace DxUtils {

UINT Align256(UINT size);
void ConfigureViewportAndScissor(UINT width, UINT height, D3D12_VIEWPORT& viewport,
                                 D3D12_RECT& scissorRect);
void ConfigureAlphaBlend(D3D12_RENDER_TARGET_BLEND_DESC& renderTarget);
void ConfigureFullscreenTrianglePipeline(D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
                                         DXGI_FORMAT renderTargetFormat,
                                         DXGI_FORMAT depthStencilFormat);

} // namespace DxUtils

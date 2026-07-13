#include "model/SkyboxRenderer.h"

#include "../graphics/internal/ConstantBufferUtils.h"
#include "../graphics/internal/RootSignatureUtils.h"
#include "core/Numeric.h"
#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "internal/SkyboxRendererInternal.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

using namespace DirectX;

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;
using GpuResourceHelpers::MapResourceChecked;
using Numeric::FiniteOr;

struct SkyboxVertex {
    XMFLOAT3 position;
};

XMFLOAT3 SanitizeFloat3(const XMFLOAT3& value) {
    return {FiniteOr(value.x, 0.0f), FiniteOr(value.y, 0.0f), FiniteOr(value.z, 0.0f)};
}

uint32_t ResolveSkyboxTextureId(const TextureManager* textureManager, uint32_t textureId) {
    if (textureManager == nullptr) {
        return kInvalidResourceId;
    }
    if (IsValidResourceId(textureId) && textureManager->IsCubeTextureId(textureId)) {
        return textureId;
    }
    const uint32_t fallbackTextureId = textureManager->GetWhiteCubeTextureId();
    return textureManager->IsCubeTextureId(fallbackTextureId) ? fallbackTextureId
                                                              : kInvalidResourceId;
}

} // namespace

SkyboxRenderer::SkyboxRenderer() : state_(std::make_unique<State>()) {}

SkyboxRenderer::~SkyboxRenderer() {
    Finalize(true);
}

bool SkyboxRenderer::IsReady() const {
    return dxCommon_ != nullptr && srvManager_ != nullptr && textureManager_ != nullptr &&
           state_->rootSignature && state_->pipelineState && state_->vertexBuffer &&
           state_->indexBuffer && HasConstantBuffers() && state_->indexCount > 0;
}

void SkyboxRenderer::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager,
                                TextureManager* textureManager) {
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager || !textureManager) {
        Finalize();
        return;
    }

    if (!Finalize()) {
        return;
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    textureManager_ = textureManager;

    CreateRootSignature();
    CreatePipelineState();
    CreateMesh();
    CreateConstantBuffer();
    if (!state_->rootSignature || !state_->pipelineState || !state_->vertexBuffer ||
        !state_->indexBuffer || !HasConstantBuffers() || state_->indexCount == 0) {
        Finalize();
    }
}

bool SkyboxRenderer::Finalize() {
    return Finalize(false);
}

bool SkyboxRenderer::Finalize(bool allowFrameAbort) {
    const bool hasGpuResources = !state_->constantFrames.empty() || state_->indexBuffer ||
                                 state_->vertexBuffer || state_->pipelineState ||
                                 state_->rootSignature;
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources, allowFrameAbort)) {
        return false;
    }

    for (ConstantFrame& frame : state_->constantFrames) {
        frame.Reset();
    }
    state_->constantFrames.clear();

    state_->indexBuffer.Reset();
    state_->vertexBuffer.Reset();
    state_->pipelineState.Reset();
    state_->rootSignature.Reset();
    state_->vbView = {};
    state_->ibView = {};
    state_->indexCount = 0;
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    textureManager_ = nullptr;
    return true;
}

void SkyboxRenderer::Draw(uint32_t textureId, const Camera& camera) {
    if (!dxCommon_ || !srvManager_ || !textureManager_ || !state_->pipelineState ||
        !state_->rootSignature || !state_->vertexBuffer || !state_->indexBuffer ||
        !HasConstantBuffers() || state_->indexCount == 0) {
        return;
    }

    const uint32_t boundTextureId = ResolveSkyboxTextureId(textureManager_, textureId);
    if (!IsValidResourceId(boundTextureId)) {
        return;
    }

    auto* cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* srvHeap = srvManager_->GetHeap();
    ConstantFrame* constantFrame = GetCurrentConstantFrame();
    if (constantFrame == nullptr || !constantFrame->resource || constantFrame->mapped == nullptr) {
        return;
    }
    const D3D12_GPU_VIRTUAL_ADDRESS constBufferAddress =
        constantFrame->resource->GetGPUVirtualAddress();
    const D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = textureManager_->GetGpuHandle(boundTextureId);
    if (cmd == nullptr || srvHeap == nullptr || constBufferAddress == 0 || textureHandle.ptr == 0) {
        return;
    }

    ID3D12DescriptorHeap* heaps[] = {srvHeap};
    cmd->SetDescriptorHeaps(1, heaps);

    cmd->SetPipelineState(state_->pipelineState.Get());
    cmd->SetGraphicsRootSignature(state_->rootSignature.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &state_->vbView);
    cmd->IASetIndexBuffer(&state_->ibView);

    const XMFLOAT3 cameraPosition = SanitizeFloat3(camera.GetPosition());
    XMMATRIX world = XMMatrixScaling(50.0f, 50.0f, 50.0f) *
                     XMMatrixTranslation(cameraPosition.x, cameraPosition.y, cameraPosition.z);
    XMMATRIX wvp = world * camera.GetView() * camera.GetProj();
    XMStoreFloat4x4(&constantFrame->mapped->matWVP, XMMatrixTranspose(wvp));

    cmd->SetGraphicsRootConstantBufferView(0, constBufferAddress);
    cmd->SetGraphicsRootDescriptorTable(1, textureHandle);
    cmd->DrawIndexedInstanced(state_->indexCount, 1, 0, 0, 0);
}

void SkyboxRenderer::CreateRootSignature() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    CD3DX12_ROOT_PARAMETER params[2]{};
    params[0].InitAsConstantBufferView(0);

    CD3DX12_DESCRIPTOR_RANGE range{};
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &range);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    RootSignatureUtils::CreateRootSignature(dxCommon_->GetDevice(), desc, state_->rootSignature);
}

void SkyboxRenderer::CreatePipelineState() {
    if (!dxCommon_ || !dxCommon_->GetDevice() || !state_->rootSignature) {
        return;
    }
    auto vs = ShaderCompiler::Compile(ShaderPaths::SkyboxVS, "main", "vs_6_6");
    auto ps = ShaderCompiler::Compile(ShaderPaths::SkyboxPS, "main", "ps_6_6");
    if (!vs || !ps) {
        return;
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = state_->rootSignature.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    desc.InputLayout = {layout, _countof(layout)};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
    desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState = rasterizer;

    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    desc.DepthStencilState = depth;

    if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&state_->pipelineState)))) {
        state_->pipelineState.Reset();
    }
}

void SkyboxRenderer::CreateMesh() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    static constexpr std::array<SkyboxVertex, 8> kVertices = {{
        {{-1.0f, -1.0f, -1.0f}},
        {{-1.0f, 1.0f, -1.0f}},
        {{1.0f, 1.0f, -1.0f}},
        {{1.0f, -1.0f, -1.0f}},
        {{-1.0f, -1.0f, 1.0f}},
        {{-1.0f, 1.0f, 1.0f}},
        {{1.0f, 1.0f, 1.0f}},
        {{1.0f, -1.0f, 1.0f}},
    }};

    static constexpr std::array<uint32_t, 36> kIndices = {{
        0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 4, 5, 1, 4, 1, 0,
        3, 2, 6, 3, 6, 7, 1, 5, 6, 1, 6, 2, 4, 0, 3, 4, 3, 7,
    }};

    state_->indexCount = static_cast<uint32_t>(kIndices.size());

    const UINT vbSize = static_cast<UINT>(sizeof(kVertices));
    const UINT ibSize = static_cast<UINT>(sizeof(kIndices));

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);

    auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
    if (!CreateCommittedResourceChecked(dxCommon_->GetDevice(), &heap, D3D12_HEAP_FLAG_NONE,
                                        &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                        state_->vertexBuffer.GetAddressOf())) {
        state_->indexCount = 0;
        return;
    }

    void* vbMapped = nullptr;
    if (!MapResourceChecked(state_->vertexBuffer.Get(), &vbMapped)) {
        state_->indexCount = 0;
        return;
    }
    memcpy(vbMapped, kVertices.data(), sizeof(kVertices));
    state_->vertexBuffer->Unmap(0, nullptr);

    state_->vbView.BufferLocation = state_->vertexBuffer->GetGPUVirtualAddress();
    state_->vbView.SizeInBytes = vbSize;
    state_->vbView.StrideInBytes = sizeof(SkyboxVertex);

    auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);
    if (!CreateCommittedResourceChecked(dxCommon_->GetDevice(), &heap, D3D12_HEAP_FLAG_NONE,
                                        &ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                        state_->indexBuffer.GetAddressOf())) {
        state_->indexCount = 0;
        return;
    }

    void* ibMapped = nullptr;
    if (!MapResourceChecked(state_->indexBuffer.Get(), &ibMapped)) {
        state_->indexCount = 0;
        return;
    }
    memcpy(ibMapped, kIndices.data(), sizeof(kIndices));
    state_->indexBuffer->Unmap(0, nullptr);

    state_->ibView.BufferLocation = state_->indexBuffer->GetGPUVirtualAddress();
    state_->ibView.SizeInBytes = ibSize;
    state_->ibView.Format = DXGI_FORMAT_R32_UINT;
}

void SkyboxRenderer::CreateConstantBuffer() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    const UINT frameCount = (std::max)(1u, dxCommon_->GetSwapChainBufferCount());
    if (!ConstantBufferUtils::CreateUploadFrames(
            dxCommon_->GetDevice(), frameCount, sizeof(ConstBufferData), state_->constantFrames,
            &ConstantFrame::resource, &ConstantFrame::mapped)) {
        return;
    }
    for (ConstantFrame& frame : state_->constantFrames) {
        XMStoreFloat4x4(&frame.mapped->matWVP, XMMatrixTranspose(XMMatrixIdentity()));
    }
}

SkyboxRenderer::ConstantFrame* SkyboxRenderer::GetCurrentConstantFrame() {
    if (state_->constantFrames.empty()) {
        return nullptr;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr ? dxCommon_->GetBackBufferIndex() % state_->constantFrames.size() : 0;
    return &state_->constantFrames[frameIndex];
}

const SkyboxRenderer::ConstantFrame* SkyboxRenderer::GetCurrentConstantFrame() const {
    if (state_->constantFrames.empty()) {
        return nullptr;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr ? dxCommon_->GetBackBufferIndex() % state_->constantFrames.size() : 0;
    return &state_->constantFrames[frameIndex];
}

bool SkyboxRenderer::HasConstantBuffers() const {
    if (state_->constantFrames.empty()) {
        return false;
    }
    return std::all_of(
        state_->constantFrames.begin(), state_->constantFrames.end(),
        [](const ConstantFrame& frame) { return frame.resource && frame.mapped != nullptr; });
}

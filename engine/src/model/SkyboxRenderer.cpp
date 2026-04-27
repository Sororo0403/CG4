#include "SkyboxRenderer.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include "ShaderCompiler.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include <array>
#include <cmath>
#include <cstring>

using namespace DirectX;
using namespace DxUtils;

namespace {

struct SkyboxVertex {
    XMFLOAT3 position;
};

bool NearlyEqual(float a, float b, float epsilon = 1.0e-6f) {
    return std::fabs(a - b) <= epsilon;
}

bool IsSameFloat3(const XMFLOAT3 &lhs, const XMFLOAT3 &rhs) {
    return NearlyEqual(lhs.x, rhs.x) && NearlyEqual(lhs.y, rhs.y) &&
           NearlyEqual(lhs.z, rhs.z);
}

bool IsSameMatrix(const XMFLOAT4X4 &lhs, const XMFLOAT4X4 &rhs) {
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            if (!NearlyEqual(lhs.m[row][column], rhs.m[row][column])) {
                return false;
            }
        }
    }

    return true;
}

} // namespace

void SkyboxRenderer::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                                TextureManager *textureManager) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    textureManager_ = textureManager;

    CreateRootSignature();
    CreatePipelineState();
    CreateMesh();
    CreateConstantBuffer();
}

void SkyboxRenderer::Draw(uint32_t textureId, const Camera &camera) {
    auto *cmd = dxCommon_->GetCommandList();

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &vbView_);
    cmd->IASetIndexBuffer(&ibView_);

    XMFLOAT4X4 currentView{};
    XMFLOAT4X4 currentProj{};
    XMStoreFloat4x4(&currentView, camera.GetView());
    XMStoreFloat4x4(&currentProj, camera.GetProj());

    const bool needsConstantBufferUpdate =
        !hasCachedCameraState_ ||
        !IsSameFloat3(camera.GetPosition(), cachedCameraPosition_) ||
        !IsSameMatrix(currentView, cachedView_) ||
        !IsSameMatrix(currentProj, cachedProj_);

    if (needsConstantBufferUpdate) {
        XMMATRIX world =
            XMMatrixScaling(50.0f, 50.0f, 50.0f) *
            XMMatrixTranslation(camera.GetPosition().x, camera.GetPosition().y,
                                camera.GetPosition().z);
        XMMATRIX wvp = world * camera.GetView() * camera.GetProj();
        XMStoreFloat4x4(&mappedCB_->matWVP, XMMatrixTranspose(wvp));

        cachedCameraPosition_ = camera.GetPosition();
        cachedView_ = currentView;
        cachedProj_ = currentProj;
        hasCachedCameraState_ = true;
    }

    cmd->SetGraphicsRootConstantBufferView(0,
                                           constBuffer_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1,
                                        textureManager_->GetGpuHandle(textureId));
    cmd->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}

void SkyboxRenderer::CreateRootSignature() {
    CD3DX12_ROOT_PARAMETER params[2]{};
    params[0].InitAsConstantBufferView(0);

    CD3DX12_DESCRIPTOR_RANGE range{};
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &range);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    Microsoft::WRL::ComPtr<ID3DBlob> blob, error;

    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "SerializeRootSignature(Skybox) failed");

    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature(Skybox) failed");
}

void SkyboxRenderer::CreatePipelineState() {
    auto vs =
        ShaderCompiler::Compile(L"resources/shaders/skybox/SkyboxVS.hlsl", "main",
                                "vs_5_0");
    auto ps =
        ShaderCompiler::Compile(L"resources/shaders/skybox/SkyboxPS.hlsl", "main",
                                "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    desc.InputLayout = {layout, _countof(layout)};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rasterizer.CullMode = D3D12_CULL_MODE_FRONT;
    desc.RasterizerState = rasterizer;

    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    D3D12_DEPTH_STENCIL_DESC depth =
        CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    desc.DepthStencilState = depth;

    ThrowIfFailed(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
                      &desc, IID_PPV_ARGS(&pipelineState_)),
                  "CreateGraphicsPipelineState(Skybox) failed");
}

void SkyboxRenderer::CreateMesh() {
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
        0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6,
        4, 5, 1, 4, 1, 0, 3, 2, 6, 3, 6, 7,
        1, 5, 6, 1, 6, 2, 4, 0, 3, 4, 3, 7,
    }};

    indexCount_ = static_cast<uint32_t>(kIndices.size());

    const UINT vbSize = static_cast<UINT>(sizeof(kVertices));
    const UINT ibSize = static_cast<UINT>(sizeof(kIndices));

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);

    auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &vbDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&vertexBuffer_)),
                  "CreateCommittedResource(SkyboxVB) failed");

    void *vbMapped = nullptr;
    ThrowIfFailed(vertexBuffer_->Map(0, nullptr, &vbMapped),
                  "Map(SkyboxVB) failed");
    memcpy(vbMapped, kVertices.data(), sizeof(kVertices));
    vertexBuffer_->Unmap(0, nullptr);

    vbView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbView_.SizeInBytes = vbSize;
    vbView_.StrideInBytes = sizeof(SkyboxVertex);

    auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);
    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &ibDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&indexBuffer_)),
                  "CreateCommittedResource(SkyboxIB) failed");

    void *ibMapped = nullptr;
    ThrowIfFailed(indexBuffer_->Map(0, nullptr, &ibMapped),
                  "Map(SkyboxIB) failed");
    memcpy(ibMapped, kIndices.data(), sizeof(kIndices));
    indexBuffer_->Unmap(0, nullptr);

    ibView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    ibView_.SizeInBytes = ibSize;
    ibView_.Format = DXGI_FORMAT_R32_UINT;
}

void SkyboxRenderer::CreateConstantBuffer() {
    const UINT size = Align256(sizeof(ConstBufferData));

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&constBuffer_)),
                  "CreateCommittedResource(SkyboxCB) failed");

    ThrowIfFailed(constBuffer_->Map(
                      0, nullptr, reinterpret_cast<void **>(&mappedCB_)),
                  "Map(SkyboxCB) failed");

    XMStoreFloat4x4(&mappedCB_->matWVP, XMMatrixTranspose(XMMatrixIdentity()));
}

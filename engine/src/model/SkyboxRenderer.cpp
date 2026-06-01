#include "model/SkyboxRenderer.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "texture/TextureManager.h"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

using namespace DirectX;

namespace {

struct SkyboxVertex {
    XMFLOAT3 position;
};

bool NearlyEqual(float a, float b, float epsilon = 1.0e-6f) {
    return std::fabs(a - b) <= epsilon;
}

float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

XMFLOAT3 SanitizeFloat3(const XMFLOAT3 &value) {
    return {FiniteOr(value.x, 0.0f), FiniteOr(value.y, 0.0f),
            FiniteOr(value.z, 0.0f)};
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

uint32_t ResolveSkyboxTextureId(TextureManager *textureManager,
                                uint32_t textureId) {
    if (textureManager == nullptr) {
        return UINT32_MAX;
    }
    if (textureId != UINT32_MAX &&
        textureManager->IsValidTextureId(textureId)) {
        return textureId;
    }
    const uint32_t fallbackTextureId = textureManager->GetWhiteCubeTextureId();
    return textureManager->IsValidTextureId(fallbackTextureId)
               ? fallbackTextureId
               : UINT32_MAX;
}

UINT Align256(size_t size) {
    if (size > static_cast<size_t>((std::numeric_limits<UINT>::max)()) - 0xFFu) {
        return 0;
    }
    return static_cast<UINT>((size + 0xFFu) & ~size_t{0xFFu});
}

bool CreateCommittedResourceChecked(
    ID3D12Device *device, const D3D12_HEAP_PROPERTIES *heapProperties,
    D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *resourceDesc,
    D3D12_RESOURCE_STATES initialState, ID3D12Resource **resource) {
    if (device == nullptr || heapProperties == nullptr ||
        resourceDesc == nullptr || resource == nullptr) {
        return false;
    }
    *resource = nullptr;
    return SUCCEEDED(device->CreateCommittedResource(
        heapProperties, heapFlags, resourceDesc, initialState, nullptr,
        IID_PPV_ARGS(resource))) &&
           *resource != nullptr;
}

bool MapResourceChecked(ID3D12Resource *resource, void **mapped) {
    if (resource == nullptr || mapped == nullptr) {
        return false;
    }
    *mapped = nullptr;
    return SUCCEEDED(resource->Map(0, nullptr, mapped)) && *mapped != nullptr;
}

} // namespace

SkyboxRenderer::~SkyboxRenderer() {
    Finalize();
}

void SkyboxRenderer::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                                TextureManager *textureManager) {
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager || !textureManager) {
        Finalize();
        return;
    }

    Finalize();

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    textureManager_ = textureManager;

    CreateRootSignature();
    CreatePipelineState();
    CreateMesh();
    CreateConstantBuffer();
    if (!rootSignature_ || !pipelineState_ || !vertexBuffer_ ||
        !indexBuffer_ || !constBuffer_ || mappedCB_ == nullptr ||
        indexCount_ == 0) {
        Finalize();
    }
}

void SkyboxRenderer::Finalize() {
    if ((constBuffer_ || indexBuffer_ || vertexBuffer_) && dxCommon_ != nullptr &&
        !dxCommon_->IsDeviceRemoved() &&
        !dxCommon_->IsCommandListRecording()) {
        dxCommon_->WaitForGpuIfPossible();
    }

    if (constBuffer_ && mappedCB_ != nullptr) {
        constBuffer_->Unmap(0, nullptr);
        mappedCB_ = nullptr;
    }

    constBuffer_.Reset();
    indexBuffer_.Reset();
    vertexBuffer_.Reset();
    pipelineState_.Reset();
    rootSignature_.Reset();
    vbView_ = {};
    ibView_ = {};
    indexCount_ = 0;
    hasCachedCameraState_ = false;
    cachedCameraPosition_ = {};
    cachedView_ = {};
    cachedProj_ = {};
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    textureManager_ = nullptr;
}

void SkyboxRenderer::Draw(uint32_t textureId, const Camera &camera) {
    if (!dxCommon_ || !srvManager_ || !textureManager_ || !pipelineState_ ||
        !rootSignature_ || !vertexBuffer_ || !indexBuffer_ || !constBuffer_ ||
        mappedCB_ == nullptr || indexCount_ == 0) {
        return;
    }

    const uint32_t boundTextureId =
        ResolveSkyboxTextureId(textureManager_, textureId);
    if (boundTextureId == UINT32_MAX) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *srvHeap = srvManager_->GetHeap();
    const D3D12_GPU_VIRTUAL_ADDRESS constBufferAddress =
        constBuffer_->GetGPUVirtualAddress();
    const D3D12_GPU_DESCRIPTOR_HANDLE textureHandle =
        textureManager_->GetGpuHandle(boundTextureId);
    if (cmd == nullptr || srvHeap == nullptr || constBufferAddress == 0 ||
        textureHandle.ptr == 0) {
        return;
    }

    ID3D12DescriptorHeap *heaps[] = {srvHeap};
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
    const XMFLOAT3 cameraPosition = SanitizeFloat3(camera.GetPosition());

    const bool needsConstantBufferUpdate =
        !hasCachedCameraState_ ||
        !IsSameFloat3(cameraPosition, cachedCameraPosition_) ||
        !IsSameMatrix(currentView, cachedView_) ||
        !IsSameMatrix(currentProj, cachedProj_);

    if (needsConstantBufferUpdate) {
        XMMATRIX world =
            XMMatrixScaling(50.0f, 50.0f, 50.0f) *
            XMMatrixTranslation(cameraPosition.x, cameraPosition.y,
                                cameraPosition.z);
        XMMATRIX wvp = world * camera.GetView() * camera.GetProj();
        XMStoreFloat4x4(&mappedCB_->matWVP, XMMatrixTranspose(wvp));

        cachedCameraPosition_ = cameraPosition;
        cachedView_ = currentView;
        cachedProj_ = currentProj;
        hasCachedCameraState_ = true;
    }

    cmd->SetGraphicsRootConstantBufferView(0, constBufferAddress);
    cmd->SetGraphicsRootDescriptorTable(1, textureHandle);
    cmd->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
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

    Microsoft::WRL::ComPtr<ID3DBlob> blob, error;

    if (FAILED(D3D12SerializeRootSignature(
            &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        !blob) {
        return;
    }

    if (FAILED(dxCommon_->GetDevice()->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature_)))) {
        rootSignature_.Reset();
    }
}

void SkyboxRenderer::CreatePipelineState() {
    if (!dxCommon_ || !dxCommon_->GetDevice() || !rootSignature_) {
        return;
    }
    auto vs =
        ShaderCompiler::Compile(ShaderPaths::SkyboxVS, "main", "vs_6_6");
    auto ps =
        ShaderCompiler::Compile(ShaderPaths::SkyboxPS, "main", "ps_6_6");
    if (!vs || !ps) {
        return;
    }

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
            &desc, IID_PPV_ARGS(&pipelineState_)))) {
        pipelineState_.Reset();
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

    indexCount_ = static_cast<uint32_t>(kIndices.size());

    const UINT vbSize = static_cast<UINT>(sizeof(kVertices));
    const UINT ibSize = static_cast<UINT>(sizeof(kIndices));

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);

    auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
    if (!CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &heap, D3D12_HEAP_FLAG_NONE, &vbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, vertexBuffer_.GetAddressOf())) {
        indexCount_ = 0;
        return;
    }

    void *vbMapped = nullptr;
    if (!MapResourceChecked(vertexBuffer_.Get(), &vbMapped)) {
        indexCount_ = 0;
        return;
    }
    memcpy(vbMapped, kVertices.data(), sizeof(kVertices));
    vertexBuffer_->Unmap(0, nullptr);

    vbView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbView_.SizeInBytes = vbSize;
    vbView_.StrideInBytes = sizeof(SkyboxVertex);

    auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);
    if (!CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &heap, D3D12_HEAP_FLAG_NONE, &ibDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, indexBuffer_.GetAddressOf())) {
        indexCount_ = 0;
        return;
    }

    void *ibMapped = nullptr;
    if (!MapResourceChecked(indexBuffer_.Get(), &ibMapped)) {
        indexCount_ = 0;
        return;
    }
    memcpy(ibMapped, kIndices.data(), sizeof(kIndices));
    indexBuffer_->Unmap(0, nullptr);

    ibView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    ibView_.SizeInBytes = ibSize;
    ibView_.Format = DXGI_FORMAT_R32_UINT;
}

void SkyboxRenderer::CreateConstantBuffer() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    const UINT size = Align256(sizeof(ConstBufferData));
    if (size == 0) {
        return;
    }

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);

    if (!CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, constBuffer_.GetAddressOf())) {
        return;
    }

    if (!MapResourceChecked(constBuffer_.Get(),
                            reinterpret_cast<void **>(&mappedCB_))) {
        return;
    }

    XMStoreFloat4x4(&mappedCB_->matWVP, XMMatrixTranspose(XMMatrixIdentity()));
}

#include "SkeletonDebugRenderer.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include "ShaderCompiler.h"
#include <algorithm>

using namespace DirectX;
using namespace DxUtils;
using Microsoft::WRL::ComPtr;

void SkeletonDebugRenderer::Initialize(DirectXCommon *dxCommon) {
    dxCommon_ = dxCommon;
    CreateRootSignature();
    CreatePipelineState();
    CreateBuffers();
}

void SkeletonDebugRenderer::Draw(const Model &model, const Transform &transform,
                                 const Camera &camera) {
    if (!dxCommon_ || model.bones.empty() || model.skeletonSpaceMatrices.empty()) {
        return;
    }

    const uint32_t lineCount = static_cast<uint32_t>(std::count_if(
        model.bones.begin(), model.bones.end(),
        [](const BoneInfo &bone) { return bone.parentIndex >= 0; }));
    if (lineCount == 0) {
        return;
    }

    const uint32_t vertexCount = lineCount * 2;
    EnsureVertexCapacity(vertexCount);
    if (!mappedVertices_) {
        return;
    }

    const XMMATRIX world = MakeWorldMatrix(model, transform);
    const XMFLOAT4 parentColor{1.0f, 0.58f, 0.05f, 1.0f};
    const XMFLOAT4 childColor{0.25f, 0.80f, 1.0f, 1.0f};

    uint32_t vertexIndex = 0;
    for (size_t jointIndex = 0; jointIndex < model.bones.size(); ++jointIndex) {
        const int parentIndex = model.bones[jointIndex].parentIndex;
        if (parentIndex < 0 ||
            static_cast<size_t>(parentIndex) >= model.skeletonSpaceMatrices.size() ||
            jointIndex >= model.skeletonSpaceMatrices.size()) {
            continue;
        }

        const XMMATRIX parentMatrix =
            XMLoadFloat4x4(&model.skeletonSpaceMatrices[parentIndex]) * world;
        const XMMATRIX childMatrix =
            XMLoadFloat4x4(&model.skeletonSpaceMatrices[jointIndex]) * world;

        XMFLOAT3 parentPosition{};
        XMFLOAT3 childPosition{};
        XMStoreFloat3(&parentPosition, parentMatrix.r[3]);
        XMStoreFloat3(&childPosition, childMatrix.r[3]);

        mappedVertices_[vertexIndex++] = {parentPosition, parentColor};
        mappedVertices_[vertexIndex++] = {childPosition, childColor};
    }

    if (vertexIndex == 0) {
        return;
    }

    XMStoreFloat4x4(&mappedViewProjection_->matViewProjection,
                    XMMatrixTranspose(camera.GetView() * camera.GetProj()));

    auto *cmd = dxCommon_->GetCommandList();
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootConstantBufferView(
        0, viewProjectionBuffer_->GetGPUVirtualAddress());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    cmd->IASetVertexBuffers(0, 1, &vertexBufferView_);
    cmd->DrawInstanced(vertexIndex, 1, 0, 0);
}

void SkeletonDebugRenderer::CreateRootSignature() {
    CD3DX12_ROOT_PARAMETER params[1];
    params[0].InitAsConstantBufferView(0);

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 0, nullptr,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                              &blob, &error),
                  "D3D12SerializeRootSignature(SkeletonDebug) failed");
    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature(SkeletonDebug) failed");
}

void SkeletonDebugRenderer::CreatePipelineState() {
    auto *device = dxCommon_->GetDevice();
    auto vs = ShaderCompiler::Compile(
        L"engine/resources/shaders/debug/SkeletonDebugVS.hlsl", "main", "vs_5_0");
    auto ps = ShaderCompiler::Compile(
        L"engine/resources/shaders/debug/SkeletonDebugPS.hlsl", "main", "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = rootSignature_.Get();
    pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pso.InputLayout = {layout, _countof(layout)};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pso.SampleDesc.Count = 1;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.BlendState = blend;

    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pso.DepthStencilState = depth;

    ThrowIfFailed(device->CreateGraphicsPipelineState(
                      &pso, IID_PPV_ARGS(&pipelineState_)),
                  "CreateGraphicsPipelineState(SkeletonDebug) failed");
}

void SkeletonDebugRenderer::CreateBuffers() {
    EnsureVertexCapacity(kInitialMaxVertices);

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(
        static_cast<UINT64>((sizeof(ViewProjectionConstBufferData) + 255) & ~255));
    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&viewProjectionBuffer_)),
                  "CreateCommittedResource(SkeletonDebugCB) failed");
    ThrowIfFailed(viewProjectionBuffer_->Map(
                      0, nullptr,
                      reinterpret_cast<void **>(&mappedViewProjection_)),
                  "Map(SkeletonDebugCB) failed");
}

void SkeletonDebugRenderer::EnsureVertexCapacity(uint32_t vertexCount) {
    if (vertexCount <= vertexCapacity_) {
        return;
    }

    vertexBuffer_.Reset();
    mappedVertices_ = nullptr;
    vertexCapacity_ = (std::max)(vertexCount, kInitialMaxVertices);

    const UINT bufferSize = sizeof(LineVertex) * vertexCapacity_;
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto vertexDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&vertexBuffer_)),
                  "CreateCommittedResource(SkeletonDebugVB) failed");
    ThrowIfFailed(vertexBuffer_->Map(0, nullptr,
                                     reinterpret_cast<void **>(&mappedVertices_)),
                  "Map(SkeletonDebugVB) failed");

    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = bufferSize;
    vertexBufferView_.StrideInBytes = sizeof(LineVertex);
}

XMMATRIX SkeletonDebugRenderer::MakeWorldMatrix(
    const Model &model, const Transform &transform) const {
    XMVECTOR q = XMQuaternionNormalize(XMLoadFloat4(&transform.rotation));
    XMMATRIX world =
        XMMatrixScaling(transform.scale.x, transform.scale.y, transform.scale.z) *
        XMMatrixRotationQuaternion(q) *
        XMMatrixTranslation(transform.position.x, transform.position.y,
                            transform.position.z);

    if (model.hasRootAnimation) {
        world = XMLoadFloat4x4(&model.rootAnimationMatrix) * world;
    }
    return world;
}

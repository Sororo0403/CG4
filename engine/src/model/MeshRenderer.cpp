#include "model/MeshRenderer.h"

#include "core/Numeric.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "RendererMaterialUtils.h"
#include "model/RendererMath.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {
using Numeric::AtLeastFinite;
using Numeric::ClampFinite;
using Numeric::FiniteOr;
using RendererMaterialUtils::PipelineVariantIndex;
using RendererMaterialUtils::ResolveBaseColorTextureId;
using RendererMaterialUtils::ResolveNormalTextureId;

struct PerObjectConstBufferData {
    XMFLOAT4X4 matWVP;
    XMFLOAT4X4 matWorld;
    XMFLOAT4X4 matWorldInverseTranspose;
};

XMFLOAT4 SanitizeFloat4(const XMFLOAT4 &value, const XMFLOAT4 &fallback) {
    return {FiniteOr(value.x, fallback.x), FiniteOr(value.y, fallback.y),
            FiniteOr(value.z, fallback.z), FiniteOr(value.w, fallback.w)};
}

bool IsDrawableMesh(const Mesh &mesh) {
    return mesh.vertexBuffer && mesh.indexBuffer && mesh.indexCount > 0 &&
           mesh.vertexStride > 0 && mesh.vbView.BufferLocation != 0 &&
           mesh.vbView.SizeInBytes > 0 &&
           mesh.vbView.StrideInBytes > 0 &&
           mesh.ibView.BufferLocation != 0 && mesh.ibView.SizeInBytes > 0 &&
           mesh.primitiveTopology != D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
}

} // namespace

MeshRenderer::~MeshRenderer() {
    Finalize(true);
}

void MeshRenderer::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                              TextureManager *textureManager) {
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
    shadowMapGpuHandle_ =
        textureManager_->GetGpuHandle(textureManager_->GetWhiteTextureId());

    CreateRootSignature();
    CreateShadowRootSignature();
    CreateGpuCullResources();
    CreateFallbackOcclusionTexture();
    CreatePipelineStates();
    CreateShadowPipelineStates();
    CreateUploadBuffer();
    if (!IsReady()) {
        Finalize();
    }
}

bool MeshRenderer::Finalize() { return Finalize(false); }

bool MeshRenderer::Finalize(bool allowFrameAbort) {
    const bool hasGpuResources =
        rootSignature_ || shadowRootSignature_ || pipelineStates_[0] ||
        instancedPipelineStates_[0] || shadowPSO_ || instancedShadowPSO_ ||
        uploadBuffer_.GetBytesPerFrame() != 0 || !customPipelines_.empty() ||
        !customInstancedPipelines_.empty() || fallbackOcclusionTexture_ ||
        fallbackOcclusionSrvIndex_ != UINT32_MAX || gpuCullRootSignature_ ||
        gpuCullPSO_ || gpuCullArgsPSO_ || gpuCullCommandSignature_ ||
        gpuLodCullRootSignature_ || gpuLodCullPSO_ || gpuLodCullArgsPSO_;
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources,
                                allowFrameAbort)) {
        return false;
    }

    ResetResources();
    return true;
}

void MeshRenderer::ResetResources() {
    fallbackOcclusionTexture_.Reset();
    if (srvManager_ != nullptr && fallbackOcclusionSrvIndex_ != UINT32_MAX) {
        srvManager_->FreeIfAllocated(fallbackOcclusionSrvIndex_);
    }
    fallbackOcclusionSrvIndex_ = UINT32_MAX;
    fallbackOcclusionGpuHandle_ = {};

    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    textureManager_ = nullptr;
    rootSignature_.Reset();
    shadowRootSignature_.Reset();
    for (auto &pipeline : pipelineStates_) {
        pipeline.Reset();
    }
    for (auto &pipeline : instancedPipelineStates_) {
        pipeline.Reset();
    }
    shadowPSO_.Reset();
    instancedShadowPSO_.Reset();
    gpuCullRootSignature_.Reset();
    gpuCullPSO_.Reset();
    gpuCullArgsPSO_.Reset();
    gpuCullCommandSignature_.Reset();
    gpuLodCullRootSignature_.Reset();
    gpuLodCullPSO_.Reset();
    gpuLodCullArgsPSO_.Reset();
    customPipelines_.clear();
    customInstancedPipelines_.clear();
    uploadBuffer_.Reset();
    InvalidateConstantCaches();
    InvalidateCommandState();
    instanceScratch_.clear();
    instanceScratch_.shrink_to_fit();
    drawIndex_ = 0;
    shadowMapGpuHandle_ = {};
    ClearOcclusionPyramid();
}

bool MeshRenderer::ReleasePipeline(uint32_t pipelineId,
                                   bool allowFrameAbort) noexcept {
    if (pipelineId >= customPipelines_.size()) {
        return false;
    }

    bool hasGpuResources = false;
    for (const auto &pipeline : customPipelines_[pipelineId].pipelineStates) {
        hasGpuResources = hasGpuResources || static_cast<bool>(pipeline);
    }
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources, allowFrameAbort)) {
        return false;
    }

    customPipelines_[pipelineId] = MeshPipelineSet{};
    InvalidateCommandState();
    return true;
}

bool MeshRenderer::ReleaseInstancedPipeline(uint32_t pipelineId,
                                            bool allowFrameAbort) noexcept {
    if (pipelineId >= customInstancedPipelines_.size()) {
        return false;
    }

    const InstancedPipelineSet &pipelineSet =
        customInstancedPipelines_[pipelineId];
    bool hasGpuResources = static_cast<bool>(pipelineSet.shadowPipelineState);
    for (const auto &pipeline : pipelineSet.pipelineStates) {
        hasGpuResources = hasGpuResources || static_cast<bool>(pipeline);
    }
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources, allowFrameAbort)) {
        return false;
    }

    customInstancedPipelines_[pipelineId] = InstancedPipelineSet{};
    InvalidateCommandState();
    return true;
}

void MeshRenderer::InvalidateConstantCaches() noexcept {
    sceneConstantsCache_ = {};
    shadowSceneConstantsCache_ = {};
    materialConstantsCache_ = {};
}

void MeshRenderer::InvalidateCommandState() noexcept {
    cachedRootSignature_ = nullptr;
    cachedPipelineState_ = nullptr;
    cachedRootParameterKinds_.fill(RootParameterKind::None);
    cachedRootParameterValues_.fill(0);
    cachedVertexBufferViews_ = {};
    cachedVertexBufferStartSlot_ = 0;
    cachedVertexBufferViewCount_ = 0;
    cachedVertexBuffersValid_ = false;
    cachedIndexBufferView_ = {};
    cachedIndexBufferValid_ = false;
    cachedPrimitiveTopology_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
}

void MeshRenderer::SetGraphicsRootSignatureCached(
    ID3D12RootSignature *rootSignature) {
    auto *cmd = dxCommon_ ? dxCommon_->GetCommandList() : nullptr;
    if (cmd == nullptr || rootSignature == nullptr) {
        return;
    }
    cmd->SetGraphicsRootSignature(rootSignature);
    cachedRootSignature_ = rootSignature;
    cachedRootParameterKinds_.fill(RootParameterKind::None);
    cachedRootParameterValues_.fill(0);
}

void MeshRenderer::SetPipelineStateCached(ID3D12PipelineState *pipelineState) {
    auto *cmd = dxCommon_ ? dxCommon_->GetCommandList() : nullptr;
    if (cmd == nullptr || pipelineState == nullptr) {
        return;
    }
    cmd->SetPipelineState(pipelineState);
    cachedPipelineState_ = pipelineState;
}

void MeshRenderer::SetGraphicsRootConstantBufferViewCached(
    uint32_t rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address) {
    auto *cmd = dxCommon_ ? dxCommon_->GetCommandList() : nullptr;
    if (cmd == nullptr || rootIndex >= cachedRootParameterValues_.size()) {
        return;
    }
    cmd->SetGraphicsRootConstantBufferView(rootIndex, address);
    cachedRootParameterKinds_[rootIndex] = RootParameterKind::ConstantBuffer;
    cachedRootParameterValues_[rootIndex] = address;
}

void MeshRenderer::SetGraphicsRootDescriptorTableCached(
    uint32_t rootIndex, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
    auto *cmd = dxCommon_ ? dxCommon_->GetCommandList() : nullptr;
    if (cmd == nullptr || rootIndex >= cachedRootParameterValues_.size()) {
        return;
    }
    cmd->SetGraphicsRootDescriptorTable(rootIndex, handle);
    cachedRootParameterKinds_[rootIndex] = RootParameterKind::DescriptorTable;
    cachedRootParameterValues_[rootIndex] = handle.ptr;
}

void MeshRenderer::IASetVertexBuffersCached(
    uint32_t startSlot, uint32_t viewCount,
    const D3D12_VERTEX_BUFFER_VIEW *views) {
    auto *cmd = dxCommon_ ? dxCommon_->GetCommandList() : nullptr;
    if (cmd == nullptr || views == nullptr || viewCount == 0u ||
        viewCount > cachedVertexBufferViews_.size()) {
        return;
    }

    cmd->IASetVertexBuffers(startSlot, viewCount, views);
    cachedVertexBufferStartSlot_ = startSlot;
    cachedVertexBufferViewCount_ = viewCount;
    cachedVertexBuffersValid_ = true;
    for (uint32_t index = 0u; index < viewCount; ++index) {
        cachedVertexBufferViews_[index] = views[index];
    }
}

void MeshRenderer::IASetIndexBufferCached(const D3D12_INDEX_BUFFER_VIEW &view) {
    auto *cmd = dxCommon_ ? dxCommon_->GetCommandList() : nullptr;
    if (cmd == nullptr) {
        return;
    }
    cmd->IASetIndexBuffer(&view);
    cachedIndexBufferView_ = view;
    cachedIndexBufferValid_ = true;
}

void MeshRenderer::IASetPrimitiveTopologyCached(
    D3D12_PRIMITIVE_TOPOLOGY topology) {
    auto *cmd = dxCommon_ ? dxCommon_->GetCommandList() : nullptr;
    if (cmd == nullptr) {
        return;
    }
    cmd->IASetPrimitiveTopology(topology);
    cachedPrimitiveTopology_ = topology;
}

bool MeshRenderer::IsReady() const {
    const auto hasAllPipelineStates = [](const auto &pipelines) {
        for (const auto &pipeline : pipelines) {
            if (!pipeline) {
                return false;
            }
        }
        return true;
    };

    return dxCommon_ != nullptr && srvManager_ != nullptr &&
           textureManager_ != nullptr && rootSignature_ &&
           shadowRootSignature_ && hasAllPipelineStates(pipelineStates_) &&
           hasAllPipelineStates(instancedPipelineStates_) && shadowPSO_ &&
           instancedShadowPSO_ && gpuCullRootSignature_ && gpuCullPSO_ &&
           gpuCullArgsPSO_ && gpuCullCommandSignature_ &&
           gpuLodCullRootSignature_ && gpuLodCullPSO_ &&
           gpuLodCullArgsPSO_ && uploadBuffer_.GetBytesPerFrame() != 0;
}

void MeshRenderer::BeginFrame() {
    if (!dxCommon_) {
        drawIndex_ = 0;
        InvalidateConstantCaches();
        return;
    }
    uploadBuffer_.BeginFrame(dxCommon_->GetBackBufferIndex());
    InvalidateConstantCaches();
    InvalidateCommandState();
}

void MeshRenderer::PreDraw() {
    if (!dxCommon_ || !srvManager_ || !rootSignature_) {
        drawIndex_ = 0;
        return;
    }
    auto *cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    if (cmd == nullptr || heap == nullptr) {
        drawIndex_ = 0;
        return;
    }
    ID3D12DescriptorHeap *heaps[] = {heap};
    InvalidateCommandState();
    cmd->SetDescriptorHeaps(1, heaps);
    SetGraphicsRootSignatureCached(rootSignature_.Get());
    drawIndex_ = 0;
}

void MeshRenderer::PostDraw() {}

void MeshRenderer::PreDrawShadow() {
    if (!dxCommon_ || !srvManager_ || !shadowRootSignature_) {
        drawIndex_ = 0;
        return;
    }
    auto *cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    if (cmd == nullptr || heap == nullptr) {
        drawIndex_ = 0;
        return;
    }
    ID3D12DescriptorHeap *heaps[] = {heap};
    InvalidateCommandState();
    cmd->SetDescriptorHeaps(1, heaps);
    SetGraphicsRootSignatureCached(shadowRootSignature_.Get());
    drawIndex_ = 0;
}
void MeshRenderer::DrawMesh(const Mesh &mesh, const Material &material,
                            const Transform &transform, const Camera &camera,
                            uint32_t textureId, uint32_t normalTextureId) {
    if (!dxCommon_ || !textureManager_ || !rootSignature_ ||
        !IsDrawableMesh(mesh) || drawIndex_ >= kMaxDraws) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    const XMMATRIX world = RendererMath::MakeWorldMatrix(transform);
    const XMMATRIX worldInverseTranspose =
        RendererMath::MakeSafeInverseTranspose(world);
    const XMMATRIX wvp = world * camera.GetView() * camera.GetProj();
    const Material drawMaterial = NormalizeMaterialForDraw(material);

    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(wvp, world, worldInverseTranspose);
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
        WriteMaterialConstants(drawMaterial);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || materialCbAddr == 0) {
        return;
    }

    if (cmd == nullptr) {
        return;
    }
    SetGraphicsRootSignatureCached(rootSignature_.Get());
    if (!SetPipelineForMaterial(drawMaterial)) {
        return;
    }
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    SetGraphicsRootDescriptorTableCached(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));
    SetGraphicsRootDescriptorTableCached(4, shadowMapGpuHandle_);
    SetGraphicsRootDescriptorTableCached(
        5, textureManager_->GetGpuHandle(
               ResolveNormalTextureId(textureManager_, drawMaterial,
                                      normalTextureId)));
    IASetVertexBuffersCached(0, 1, &mesh.vbView);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);

    ++drawIndex_;
}

void MeshRenderer::DrawMeshWithPipeline(
    uint32_t pipelineId, const Mesh &mesh, const Material &material,
    const Transform &transform, const Camera &camera, uint32_t textureId,
    uint32_t normalTextureId) {
    if (!dxCommon_ || !textureManager_ || !rootSignature_ ||
        pipelineId >= customPipelines_.size() || !IsDrawableMesh(mesh) ||
        drawIndex_ >= kMaxDraws) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    const XMMATRIX world = RendererMath::MakeWorldMatrix(transform);
    const XMMATRIX worldInverseTranspose =
        RendererMath::MakeSafeInverseTranspose(world);
    const XMMATRIX wvp = world * camera.GetView() * camera.GetProj();
    const Material drawMaterial = NormalizeMaterialForDraw(material);

    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(wvp, world, worldInverseTranspose);
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
        WriteMaterialConstants(drawMaterial);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || materialCbAddr == 0) {
        return;
    }

    if (cmd == nullptr) {
        return;
    }
    SetGraphicsRootSignatureCached(rootSignature_.Get());
    if (!SetPipelineForMaterial(customPipelines_[pipelineId].pipelineStates,
                                drawMaterial)) {
        return;
    }
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    SetGraphicsRootDescriptorTableCached(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));
    SetGraphicsRootDescriptorTableCached(4, shadowMapGpuHandle_);
    SetGraphicsRootDescriptorTableCached(
        5, textureManager_->GetGpuHandle(
               ResolveNormalTextureId(textureManager_, drawMaterial,
                                      normalTextureId)));
    IASetVertexBuffersCached(0, 1, &mesh.vbView);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);

    ++drawIndex_;
}

void MeshRenderer::DrawMeshWithPipelineHandles(
    uint32_t pipelineId, const Mesh &mesh, const Material &material,
    const Transform &transform, const Camera &camera,
    D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE normalTextureHandle) {
    if (!dxCommon_ || !textureManager_ || !rootSignature_ ||
        pipelineId >= customPipelines_.size() || !IsDrawableMesh(mesh) ||
        drawIndex_ >= kMaxDraws) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    const XMMATRIX world = RendererMath::MakeWorldMatrix(transform);
    const XMMATRIX worldInverseTranspose =
        RendererMath::MakeSafeInverseTranspose(world);
    const XMMATRIX wvp = world * camera.GetView() * camera.GetProj();
    const Material drawMaterial = NormalizeMaterialForDraw(material);

    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(wvp, world, worldInverseTranspose);
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
        WriteMaterialConstants(drawMaterial);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || materialCbAddr == 0) {
        return;
    }

    if (cmd == nullptr) {
        return;
    }
    SetGraphicsRootSignatureCached(rootSignature_.Get());
    if (!SetPipelineForMaterial(customPipelines_[pipelineId].pipelineStates,
                                drawMaterial)) {
        return;
    }
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    const D3D12_GPU_DESCRIPTOR_HANDLE baseColorHandle =
        textureHandle.ptr != 0
            ? textureHandle
            : textureManager_->GetGpuHandle(textureManager_->GetWhiteTextureId());
    const D3D12_GPU_DESCRIPTOR_HANDLE normalHandle =
        normalTextureHandle.ptr != 0
            ? normalTextureHandle
            : textureManager_->GetGpuHandle(
                  textureManager_->GetDefaultNormalTextureId());
    SetGraphicsRootDescriptorTableCached(3, baseColorHandle);
    SetGraphicsRootDescriptorTableCached(4, shadowMapGpuHandle_);
    SetGraphicsRootDescriptorTableCached(5, normalHandle);
    IASetVertexBuffersCached(0, 1, &mesh.vbView);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);

    ++drawIndex_;
}

void MeshRenderer::DrawMeshInstanced(const Mesh &mesh, const Material &material,
                                     const InstanceData *instances,
                                     uint32_t instanceCount,
                                     const Camera &camera,
                                     uint32_t textureId,
                                     uint32_t normalTextureId) {
    if (!dxCommon_ || !textureManager_ || !rootSignature_ ||
        !IsDrawableMesh(mesh) || !instances || instanceCount == 0 ||
        drawIndex_ >= kMaxDraws) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    const Material drawMaterial = NormalizeMaterialForDraw(material);

    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(),
                             XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
        WriteMaterialConstants(drawMaterial);
    const D3D12_VERTEX_BUFFER_VIEW instanceView =
        WriteInstances(instances, instanceCount);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || materialCbAddr == 0 ||
        instanceView.BufferLocation == 0) {
        return;
    }

    if (cmd == nullptr) {
        return;
    }
    SetGraphicsRootSignatureCached(rootSignature_.Get());
    if (!SetInstancedPipelineForMaterial(drawMaterial)) {
        return;
    }
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    SetGraphicsRootDescriptorTableCached(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));
    SetGraphicsRootDescriptorTableCached(4, shadowMapGpuHandle_);
    SetGraphicsRootDescriptorTableCached(
        5, textureManager_->GetGpuHandle(
               ResolveNormalTextureId(textureManager_, drawMaterial,
                                      normalTextureId)));
    D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, instanceView};
    IASetVertexBuffersCached(0, 2, views);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);

    ++drawIndex_;
}

void MeshRenderer::DrawMeshInstancedWithPipeline(
    uint32_t pipelineId, const Mesh &mesh, const Material &material,
    const InstanceData *instances, uint32_t instanceCount, const Camera &camera,
    uint32_t textureId, uint32_t normalTextureId) {
    if (!dxCommon_ || !textureManager_ || !rootSignature_ ||
        pipelineId >= customInstancedPipelines_.size() ||
        !IsDrawableMesh(mesh) || !instances || instanceCount == 0 ||
        drawIndex_ >= kMaxDraws) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    const Material drawMaterial = NormalizeMaterialForDraw(material);

    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(),
                             XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
        WriteMaterialConstants(drawMaterial);
    const D3D12_VERTEX_BUFFER_VIEW instanceView =
        WriteInstances(instances, instanceCount);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || materialCbAddr == 0 ||
        instanceView.BufferLocation == 0) {
        return;
    }

    if (cmd == nullptr) {
        return;
    }
    SetGraphicsRootSignatureCached(rootSignature_.Get());
    if (!SetInstancedPipelineForMaterial(
            customInstancedPipelines_[pipelineId].pipelineStates,
            drawMaterial)) {
        return;
    }
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    SetGraphicsRootDescriptorTableCached(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));
    SetGraphicsRootDescriptorTableCached(4, shadowMapGpuHandle_);
    SetGraphicsRootDescriptorTableCached(
        5, textureManager_->GetGpuHandle(
               ResolveNormalTextureId(textureManager_, drawMaterial,
                                      normalTextureId)));
    D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, instanceView};
    IASetVertexBuffersCached(0, 2, views);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);

    ++drawIndex_;
}

void MeshRenderer::DrawMeshInstancedWithPipeline(
    uint32_t pipelineId, const Mesh &mesh, const Material &material,
    const MeshInstanceBuffer &instanceBuffer, const Camera &camera,
    uint32_t textureId, uint32_t normalTextureId) {
    if (!instanceBuffer.IsValid()) {
        return;
    }
    const Material drawMaterial = NormalizeMaterialForDraw(material);
    DrawInstancedWithPreparedBuffer(
        pipelineId, mesh, drawMaterial, instanceBuffer.view,
        instanceBuffer.instanceCount, camera, textureId, normalTextureId);
}

bool MeshRenderer::DrawInstancedWithPreparedBuffer(
    uint32_t pipelineId, const Mesh &mesh, const Material &drawMaterial,
    const D3D12_VERTEX_BUFFER_VIEW &instanceView, uint32_t instanceCount,
    const Camera &camera, uint32_t textureId, uint32_t normalTextureId) {
    if (!dxCommon_ || !textureManager_ || !rootSignature_ ||
        pipelineId >= customInstancedPipelines_.size() ||
        !IsDrawableMesh(mesh) || instanceCount == 0 ||
        instanceView.BufferLocation == 0 || drawIndex_ >= kMaxDraws) {
        return false;
    }

    auto *cmd = dxCommon_->GetCommandList();
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(),
                             XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
        WriteMaterialConstants(drawMaterial);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || materialCbAddr == 0) {
        return false;
    }

    if (cmd == nullptr) {
        return false;
    }
    SetGraphicsRootSignatureCached(rootSignature_.Get());
    if (!SetInstancedPipelineForMaterial(
            customInstancedPipelines_[pipelineId].pipelineStates,
            drawMaterial)) {
        return false;
    }
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    SetGraphicsRootDescriptorTableCached(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));
    SetGraphicsRootDescriptorTableCached(4, shadowMapGpuHandle_);
    SetGraphicsRootDescriptorTableCached(
        5, textureManager_->GetGpuHandle(
               ResolveNormalTextureId(textureManager_, drawMaterial,
                                      normalTextureId)));
    D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, instanceView};
    IASetVertexBuffersCached(0, 2, views);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);

    ++drawIndex_;
    return true;
}
void MeshRenderer::DrawMeshShadow(
    const Mesh &mesh, const Transform &transform,
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
    DrawMeshShadow(mesh, Material{}, transform, lightViewProjection, 0);
}

void MeshRenderer::DrawMeshShadow(
    const Mesh &mesh, const Material &material, const Transform &transform,
    const DirectX::XMFLOAT4X4 &lightViewProjection, uint32_t textureId) {
    if (!dxCommon_ || !textureManager_ || !shadowRootSignature_ ||
        !shadowPSO_ || !IsDrawableMesh(mesh) || drawIndex_ >= kMaxDraws) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    const XMMATRIX world = RendererMath::MakeWorldMatrix(transform);
    const XMMATRIX lightVP = XMLoadFloat4x4(&lightViewProjection);
    const XMMATRIX wvp = world * lightVP;
    const Material drawMaterial = NormalizeMaterialForDraw(material);

    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(wvp, world, XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr =
        WriteShadowSceneConstants(lightViewProjection);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
        WriteMaterialConstants(drawMaterial);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || materialCbAddr == 0) {
        return;
    }
    if (cmd == nullptr) {
        return;
    }
    SetGraphicsRootSignatureCached(shadowRootSignature_.Get());
    SetPipelineStateCached(shadowPSO_.Get());
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    SetGraphicsRootDescriptorTableCached(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));
    IASetVertexBuffersCached(0, 1, &mesh.vbView);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
    ++drawIndex_;
}

void MeshRenderer::DrawMeshInstancedShadow(
    const Mesh &mesh, const InstanceData *instances, uint32_t instanceCount,
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
    DrawMeshInstancedShadow(mesh, Material{}, instances, instanceCount,
                            lightViewProjection, 0);
}

void MeshRenderer::DrawMeshInstancedShadow(
    const Mesh &mesh, const Material &material, const InstanceData *instances,
    uint32_t instanceCount, const DirectX::XMFLOAT4X4 &lightViewProjection,
    uint32_t textureId) {
    if (!dxCommon_ || !textureManager_ || !shadowRootSignature_ ||
        !instancedShadowPSO_ || !IsDrawableMesh(mesh) || !instances ||
        instanceCount == 0 || drawIndex_ >= kMaxDraws) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    const Material drawMaterial = NormalizeMaterialForDraw(material);

    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(),
                             XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr =
        WriteShadowSceneConstants(lightViewProjection);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
        WriteMaterialConstants(drawMaterial);
    const D3D12_VERTEX_BUFFER_VIEW instanceView =
        WriteInstances(instances, instanceCount);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || materialCbAddr == 0 ||
        instanceView.BufferLocation == 0) {
        return;
    }

    if (cmd == nullptr) {
        return;
    }
    SetGraphicsRootSignatureCached(shadowRootSignature_.Get());
    SetPipelineStateCached(instancedShadowPSO_.Get());
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    SetGraphicsRootDescriptorTableCached(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));

    D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, instanceView};
    IASetVertexBuffersCached(0, 2, views);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);
    ++drawIndex_;
}

void MeshRenderer::DrawMeshInstancedShadowWithPipeline(
    uint32_t pipelineId, const Mesh &mesh, const Material &material,
    const InstanceData *instances, uint32_t instanceCount,
    const DirectX::XMFLOAT4X4 &lightViewProjection, uint32_t textureId) {
    if (!dxCommon_ || !textureManager_ || !shadowRootSignature_ ||
        pipelineId >= customInstancedPipelines_.size() ||
        !IsDrawableMesh(mesh) || !instances || instanceCount == 0 ||
        drawIndex_ >= kMaxDraws) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    const Material drawMaterial = NormalizeMaterialForDraw(material);

    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(),
                             XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr =
        WriteShadowSceneConstants(lightViewProjection);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
        WriteMaterialConstants(drawMaterial);
    const D3D12_VERTEX_BUFFER_VIEW instanceView =
        WriteInstances(instances, instanceCount);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || materialCbAddr == 0 ||
        instanceView.BufferLocation == 0) {
        return;
    }

    if (cmd == nullptr ||
        !customInstancedPipelines_[pipelineId].shadowPipelineState) {
        return;
    }
    SetGraphicsRootSignatureCached(shadowRootSignature_.Get());
    SetPipelineStateCached(
        customInstancedPipelines_[pipelineId].shadowPipelineState.Get());
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    SetGraphicsRootDescriptorTableCached(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));

    D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, instanceView};
    IASetVertexBuffersCached(0, 2, views);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);
    ++drawIndex_;
}

void MeshRenderer::DrawMeshInstancedShadowWithPipeline(
    uint32_t pipelineId, const Mesh &mesh, const Material &material,
    const MeshInstanceBuffer &instanceBuffer,
    const DirectX::XMFLOAT4X4 &lightViewProjection, uint32_t textureId) {
    if (!instanceBuffer.IsValid()) {
        return;
    }
    const Material drawMaterial = NormalizeMaterialForDraw(material);
    DrawInstancedShadowWithPreparedBuffer(
        pipelineId, mesh, drawMaterial, instanceBuffer.view,
        instanceBuffer.instanceCount, lightViewProjection, textureId);
}

bool MeshRenderer::DrawInstancedShadowWithPreparedBuffer(
    uint32_t pipelineId, const Mesh &mesh, const Material &drawMaterial,
    const D3D12_VERTEX_BUFFER_VIEW &instanceView, uint32_t instanceCount,
    const DirectX::XMFLOAT4X4 &lightViewProjection, uint32_t textureId) {
    if (!dxCommon_ || !textureManager_ || !shadowRootSignature_ ||
        pipelineId >= customInstancedPipelines_.size() ||
        !IsDrawableMesh(mesh) || instanceCount == 0 ||
        instanceView.BufferLocation == 0 || drawIndex_ >= kMaxDraws) {
        return false;
    }

    auto *cmd = dxCommon_->GetCommandList();
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(),
                             XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr =
        WriteShadowSceneConstants(lightViewProjection);
    const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
        WriteMaterialConstants(drawMaterial);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || materialCbAddr == 0) {
        return false;
    }

    if (cmd == nullptr ||
        !customInstancedPipelines_[pipelineId].shadowPipelineState) {
        return false;
    }
    SetGraphicsRootSignatureCached(shadowRootSignature_.Get());
    SetPipelineStateCached(
        customInstancedPipelines_[pipelineId].shadowPipelineState.Get());
    SetGraphicsRootConstantBufferViewCached(0, objectCbAddr);
    SetGraphicsRootConstantBufferViewCached(1, sceneCbAddr);
    SetGraphicsRootConstantBufferViewCached(2, materialCbAddr);
    SetGraphicsRootDescriptorTableCached(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));

    D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, instanceView};
    IASetVertexBuffersCached(0, 2, views);
    IASetIndexBufferCached(mesh.ibView);
    IASetPrimitiveTopologyCached(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);
    ++drawIndex_;
    return true;
}
bool MeshRenderer::SetPipelineForMaterial(const Material &material) {
    auto *cmd = dxCommon_ ? dxCommon_->GetCommandList() : nullptr;
    ID3D12PipelineState *pipelineState =
        pipelineStates_[PipelineVariantIndex(material)].Get();
    if (cmd == nullptr || pipelineState == nullptr) {
        return false;
    }
    SetPipelineStateCached(pipelineState);
    return true;
}

bool MeshRenderer::SetPipelineForMaterial(
    const std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
                     kPipelineVariantCount> &pipelineStates,
    const Material &material) {
    auto *cmd = dxCommon_ ? dxCommon_->GetCommandList() : nullptr;
    ID3D12PipelineState *pipelineState =
        pipelineStates[PipelineVariantIndex(material)].Get();
    if (cmd == nullptr || pipelineState == nullptr) {
        return false;
    }
    SetPipelineStateCached(pipelineState);
    return true;
}

bool MeshRenderer::SetInstancedPipelineForMaterial(const Material &material) {
    auto *cmd = dxCommon_ ? dxCommon_->GetCommandList() : nullptr;
    ID3D12PipelineState *pipelineState =
        instancedPipelineStates_[PipelineVariantIndex(material)].Get();
    if (cmd == nullptr || pipelineState == nullptr) {
        return false;
    }
    SetPipelineStateCached(pipelineState);
    return true;
}

bool MeshRenderer::SetInstancedPipelineForMaterial(
    const std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
                     kPipelineVariantCount> &pipelineStates,
    const Material &material) {
    auto *cmd = dxCommon_ ? dxCommon_->GetCommandList() : nullptr;
    ID3D12PipelineState *pipelineState =
        pipelineStates[PipelineVariantIndex(material)].Get();
    if (cmd == nullptr || pipelineState == nullptr) {
        return false;
    }
    SetPipelineStateCached(pipelineState);
    return true;
}

void MeshRenderer::SetShadowMap(
    D3D12_GPU_DESCRIPTOR_HANDLE shadowMap,
    const DirectX::XMFLOAT4X4 &lightViewProjection,
    const SceneShadowSettings &settings) {
    if (!textureManager_) {
        shadowMapGpuHandle_ = {};
        shadowLightViewProjection_ = lightViewProjection;
        shadowParams_ = {};
        shadowFilterParams_ = {};
        InvalidateConstantCaches();
        return;
    }
    const bool hasShadowMap = shadowMap.ptr != 0;
    shadowMapGpuHandle_ =
        hasShadowMap
            ? shadowMap
            : textureManager_->GetGpuHandle(textureManager_->GetWhiteTextureId());
    shadowLightViewProjection_ = lightViewProjection;
    shadowParams_ = {
        hasShadowMap ? 1.0f : 0.0f,
        std::isfinite(settings.bias) ? settings.bias : 0.0f,
        ClampFinite(settings.strength, 0.0f, 1.0f, 0.0f),
        std::isfinite(settings.normalBias) ? settings.normalBias : 0.0f};
    shadowFilterParams_ = {
        AtLeastFinite(settings.filterRadius, 0.0f, 0.0f),
        AtLeastFinite(settings.depthSoftness, 0.0001f, 0.0001f),
        AtLeastFinite(settings.edgeFade, 0.0f, 0.0f), 0.0f};
    InvalidateConstantCaches();
}

void MeshRenderer::SetOcclusionPyramid(
    D3D12_GPU_DESCRIPTOR_HANDLE depthPyramid,
    const DirectX::XMMATRIX &viewProjection, uint32_t width, uint32_t height,
    uint32_t mipCount, float depthBias) {
    if (depthPyramid.ptr == 0 || width == 0u || height == 0u ||
        mipCount == 0u) {
        ClearOcclusionPyramid();
        return;
    }

    XMStoreFloat4x4(&occlusionViewProjection_,
                    XMMatrixTranspose(viewProjection));
    occlusionParams_ = {
        static_cast<float>(width),
        static_cast<float>(height),
        static_cast<float>(mipCount),
        ClampFinite(depthBias, 0.0f, 0.05f, 0.006f)};
    occlusionPyramidGpuHandle_ = depthPyramid;
    occlusionPyramidEnabled_ = true;
}

void MeshRenderer::ClearOcclusionPyramid() {
    occlusionPyramidGpuHandle_ = {};
    occlusionParams_ = {0.0f, 0.0f, 0.0f, 0.006f};
    occlusionPyramidEnabled_ = false;
}

void MeshRenderer::SetCustomSceneParams(const DirectX::XMFLOAT4 &params0,
                                        const DirectX::XMFLOAT4 &params1) {
    customSceneParams0_ = SanitizeFloat4(params0, customSceneParams0_);
    customSceneParams1_ = SanitizeFloat4(params1, customSceneParams1_);
    InvalidateConstantCaches();
}

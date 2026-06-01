#include "model/MeshRenderer.h"

#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {

struct PerObjectConstBufferData {
    XMFLOAT4X4 matWVP;
    XMFLOAT4X4 matWorld;
    XMFLOAT4X4 matWorldInverseTranspose;
};

struct SceneConstBufferData {
    struct PointLightData {
        XMFLOAT4 positionRange;
        XMFLOAT4 colorIntensity;
    };

    XMFLOAT4 cameraPos;
    XMFLOAT4 keyLightDirection;
    XMFLOAT4 keyLightColor;
    XMFLOAT4 fillLightDirection;
    XMFLOAT4 fillLightColor;
    XMFLOAT4 ambientColor;
    PointLightData pointLights[2];
    XMFLOAT4 lightingParams;
    XMFLOAT4 fogColor;
    XMFLOAT4 fogParams;
    XMFLOAT4X4 viewProjection;
    XMFLOAT4X4 lightViewProjection;
    XMFLOAT4 shadowParams;
    XMFLOAT4 shadowFilterParams;
    XMFLOAT4 customSceneParams0;
    XMFLOAT4 customSceneParams1;
};

XMVECTOR LoadNormalizedQuaternionOrIdentity(const XMFLOAT4 &rotation) {
    if (!std::isfinite(rotation.x) || !std::isfinite(rotation.y) ||
        !std::isfinite(rotation.z) || !std::isfinite(rotation.w)) {
        return XMQuaternionIdentity();
    }
    XMVECTOR q = XMLoadFloat4(&rotation);
    const float lengthSq = XMVectorGetX(XMVector4LengthSq(q));
    if (!std::isfinite(lengthSq) || lengthSq <= 0.000001f) {
        return XMQuaternionIdentity();
    }
    return XMQuaternionNormalize(q);
}

float ClampFinite(float value, float minimum, float maximum, float fallback) {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

float ClampFiniteMin(float value, float minimum) {
    if (!std::isfinite(value)) {
        return minimum;
    }
    return (std::max)(value, minimum);
}

XMMATRIX MakeWorldMatrix(const Transform &transform) {
    const Transform safeTransform = SanitizeTransformForDraw(transform);
    XMVECTOR q = LoadNormalizedQuaternionOrIdentity(safeTransform.rotation);
    return XMMatrixScaling(safeTransform.scale.x, safeTransform.scale.y,
                           safeTransform.scale.z) *
           XMMatrixRotationQuaternion(q) *
           XMMatrixTranslation(safeTransform.position.x,
                               safeTransform.position.y,
                               safeTransform.position.z);
}

XMMATRIX MakeWorldInverseTranspose(const XMMATRIX &world) {
    const XMVECTOR determinant = XMMatrixDeterminant(world);
    const float determinantValue = XMVectorGetX(determinant);
    if (!std::isfinite(determinantValue) ||
        std::abs(determinantValue) <= 0.000001f) {
        return XMMatrixIdentity();
    }
    return XMMatrixTranspose(XMMatrixInverse(nullptr, world));
}

bool IsTransparentMaterial(const Material &material) {
    return material.blendMode == static_cast<int32_t>(BlendMode::Transparent) ||
           material.color.w < 1.0f;
}

D3D12_CULL_MODE ToD3D12CullMode(const MaterialCullMode mode) {
    switch (mode) {
    case MaterialCullMode::None:
        return D3D12_CULL_MODE_NONE;
    case MaterialCullMode::Front:
        return D3D12_CULL_MODE_FRONT;
    case MaterialCullMode::Back:
    default:
        return D3D12_CULL_MODE_BACK;
    }
}

size_t PipelineVariantIndex(bool transparent, MaterialCullMode cullMode,
                            bool depthWrite) {
    const size_t blendIndex = transparent ? 1 : 0;
    const size_t cullIndex = static_cast<size_t>(cullMode);
    const size_t depthIndex = depthWrite ? 1 : 0;
    return blendIndex * 6 + cullIndex * 2 + depthIndex;
}

size_t PipelineVariantIndex(const Material &material) {
    const Material drawMaterial = NormalizeMaterialForDraw(material);
    MaterialCullMode cullMode =
        static_cast<MaterialCullMode>(drawMaterial.cullMode);
    if (drawMaterial.cullMode < static_cast<int32_t>(MaterialCullMode::None) ||
        drawMaterial.cullMode > static_cast<int32_t>(MaterialCullMode::Back)) {
        cullMode = MaterialCullMode::Back;
    }
    return PipelineVariantIndex(IsTransparentMaterial(drawMaterial), cullMode,
                                drawMaterial.depthWrite != 0);
}

uint32_t ResolveTextureId(TextureManager *textureManager, uint32_t textureId,
                          uint32_t fallbackTextureId) {
    if (textureManager == nullptr) {
        return UINT32_MAX;
    }
    if (textureId != UINT32_MAX &&
        textureManager->IsValidTextureId(textureId)) {
        return textureId;
    }
    if (fallbackTextureId != UINT32_MAX &&
        textureManager->IsValidTextureId(fallbackTextureId)) {
        return fallbackTextureId;
    }
    return textureManager->GetWhiteTextureId();
}

uint32_t ResolveNormalTextureId(TextureManager *textureManager,
                                uint32_t normalTextureId) {
    const uint32_t fallbackTextureId =
        textureManager != nullptr ? textureManager->GetDefaultNormalTextureId()
                                  : UINT32_MAX;
    return ResolveTextureId(textureManager, normalTextureId,
                            fallbackTextureId);
}

uint32_t ResolveBaseColorTextureId(TextureManager *textureManager,
                                   const Material &material,
                                   uint32_t fallbackTextureId) {
    const uint32_t textureId = material.baseColorTextureId == UINT32_MAX
                                   ? fallbackTextureId
                                   : material.baseColorTextureId;
    return ResolveTextureId(textureManager, textureId, fallbackTextureId);
}

uint32_t ResolveNormalTextureId(TextureManager *textureManager,
                                const Material &material,
                                uint32_t fallbackTextureId) {
    const uint32_t textureId = material.normalTextureId == UINT32_MAX
                                   ? fallbackTextureId
                                   : material.normalTextureId;
    return ResolveNormalTextureId(textureManager, textureId);
}

} // namespace

void MeshRenderer::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                              TextureManager *textureManager) {
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager || !textureManager) {
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
        customPipelines_.clear();
        customInstancedPipelines_.clear();
        uploadBuffer_.Reset();
        drawIndex_ = 0;
        shadowMapGpuHandle_ = {};
        return;
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    textureManager_ = textureManager;
    shadowMapGpuHandle_ =
        textureManager_->GetGpuHandle(textureManager_->GetWhiteTextureId());

    CreateRootSignature();
    CreateShadowRootSignature();
    CreatePipelineStates();
    CreateShadowPipelineStates();
    CreateUploadBuffer();
    if (!rootSignature_ || !shadowRootSignature_ || !pipelineStates_[0] ||
        !instancedPipelineStates_[0] || !shadowPSO_ ||
        !instancedShadowPSO_ || uploadBuffer_.GetBytesPerFrame() == 0) {
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
        customPipelines_.clear();
        customInstancedPipelines_.clear();
        uploadBuffer_.Reset();
        drawIndex_ = 0;
        shadowMapGpuHandle_ = {};
    }
}

void MeshRenderer::BeginFrame() {
    if (!dxCommon_) {
        drawIndex_ = 0;
        return;
    }
    uploadBuffer_.BeginFrame(dxCommon_->GetBackBufferIndex());
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
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
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
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
    drawIndex_ = 0;
}

void MeshRenderer::DrawMesh(const Mesh &mesh, const Material &material,
                            const Transform &transform, const Camera &camera,
                            uint32_t textureId, uint32_t normalTextureId) {
    if (!dxCommon_ || !textureManager_ || !rootSignature_ ||
        drawIndex_ >= kMaxDraws) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    const XMMATRIX world = MakeWorldMatrix(transform);
    const XMMATRIX worldInverseTranspose = MakeWorldInverseTranspose(world);
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

    if (cmd == nullptr || !SetPipelineForMaterial(drawMaterial)) {
        return;
    }
    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
    cmd->SetGraphicsRootConstantBufferView(2, materialCbAddr);
    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));
    cmd->SetGraphicsRootDescriptorTable(4, shadowMapGpuHandle_);
    cmd->SetGraphicsRootDescriptorTable(
        5, textureManager_->GetGpuHandle(
               ResolveNormalTextureId(textureManager_, drawMaterial,
                                      normalTextureId)));
    cmd->IASetVertexBuffers(0, 1, &mesh.vbView);
    cmd->IASetIndexBuffer(&mesh.ibView);
    cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);

    ++drawIndex_;
}

void MeshRenderer::DrawMeshWithPipeline(
    uint32_t pipelineId, const Mesh &mesh, const Material &material,
    const Transform &transform, const Camera &camera, uint32_t textureId,
    uint32_t normalTextureId) {
    if (!dxCommon_ || !textureManager_ || !rootSignature_ ||
        pipelineId >= customPipelines_.size() || drawIndex_ >= kMaxDraws) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    const XMMATRIX world = MakeWorldMatrix(transform);
    const XMMATRIX worldInverseTranspose = MakeWorldInverseTranspose(world);
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

    if (cmd == nullptr ||
        !SetPipelineForMaterial(customPipelines_[pipelineId].pipelineStates,
                                drawMaterial)) {
        return;
    }
    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
    cmd->SetGraphicsRootConstantBufferView(2, materialCbAddr);
    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));
    cmd->SetGraphicsRootDescriptorTable(4, shadowMapGpuHandle_);
    cmd->SetGraphicsRootDescriptorTable(
        5, textureManager_->GetGpuHandle(
               ResolveNormalTextureId(textureManager_, drawMaterial,
                                      normalTextureId)));
    cmd->IASetVertexBuffers(0, 1, &mesh.vbView);
    cmd->IASetIndexBuffer(&mesh.ibView);
    cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);

    ++drawIndex_;
}

void MeshRenderer::DrawMeshWithPipelineHandles(
    uint32_t pipelineId, const Mesh &mesh, const Material &material,
    const Transform &transform, const Camera &camera,
    D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE normalTextureHandle) {
    if (!dxCommon_ || !textureManager_ || !rootSignature_ ||
        pipelineId >= customPipelines_.size() || drawIndex_ >= kMaxDraws) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    const XMMATRIX world = MakeWorldMatrix(transform);
    const XMMATRIX worldInverseTranspose = MakeWorldInverseTranspose(world);
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

    if (cmd == nullptr ||
        !SetPipelineForMaterial(customPipelines_[pipelineId].pipelineStates,
                                drawMaterial)) {
        return;
    }
    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
    cmd->SetGraphicsRootConstantBufferView(2, materialCbAddr);
    const D3D12_GPU_DESCRIPTOR_HANDLE baseColorHandle =
        textureHandle.ptr != 0
            ? textureHandle
            : textureManager_->GetGpuHandle(textureManager_->GetWhiteTextureId());
    const D3D12_GPU_DESCRIPTOR_HANDLE normalHandle =
        normalTextureHandle.ptr != 0
            ? normalTextureHandle
            : textureManager_->GetGpuHandle(
                  textureManager_->GetDefaultNormalTextureId());
    cmd->SetGraphicsRootDescriptorTable(3, baseColorHandle);
    cmd->SetGraphicsRootDescriptorTable(4, shadowMapGpuHandle_);
    cmd->SetGraphicsRootDescriptorTable(5, normalHandle);
    cmd->IASetVertexBuffers(0, 1, &mesh.vbView);
    cmd->IASetIndexBuffer(&mesh.ibView);
    cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);

    ++drawIndex_;
}

void MeshRenderer::DrawMeshInstanced(const Mesh &mesh, const Material &material,
                                     const InstanceData *instances,
                                     uint32_t instanceCount,
                                     const Camera &camera,
                                     uint32_t textureId,
                                     uint32_t normalTextureId) {
    if (!dxCommon_ || !textureManager_ || !rootSignature_ || !instances ||
        instanceCount == 0 || drawIndex_ >= kMaxDraws) {
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

    if (cmd == nullptr || !SetInstancedPipelineForMaterial(drawMaterial)) {
        return;
    }
    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
    cmd->SetGraphicsRootConstantBufferView(2, materialCbAddr);
    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));
    cmd->SetGraphicsRootDescriptorTable(4, shadowMapGpuHandle_);
    cmd->SetGraphicsRootDescriptorTable(
        5, textureManager_->GetGpuHandle(
               ResolveNormalTextureId(textureManager_, drawMaterial,
                                      normalTextureId)));
    D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, instanceView};
    cmd->IASetVertexBuffers(0, 2, views);
    cmd->IASetIndexBuffer(&mesh.ibView);
    cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);

    ++drawIndex_;
}

void MeshRenderer::DrawMeshInstancedWithPipeline(
    uint32_t pipelineId, const Mesh &mesh, const Material &material,
    const InstanceData *instances, uint32_t instanceCount, const Camera &camera,
    uint32_t textureId, uint32_t normalTextureId) {
    if (!dxCommon_ || !textureManager_ || !rootSignature_ ||
        pipelineId >= customInstancedPipelines_.size() || !instances ||
        instanceCount == 0 || drawIndex_ >= kMaxDraws) {
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

    if (cmd == nullptr ||
        !SetInstancedPipelineForMaterial(
            customInstancedPipelines_[pipelineId].pipelineStates,
            drawMaterial)) {
        return;
    }
    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
    cmd->SetGraphicsRootConstantBufferView(2, materialCbAddr);
    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));
    cmd->SetGraphicsRootDescriptorTable(4, shadowMapGpuHandle_);
    cmd->SetGraphicsRootDescriptorTable(
        5, textureManager_->GetGpuHandle(
               ResolveNormalTextureId(textureManager_, drawMaterial,
                                      normalTextureId)));
    D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, instanceView};
    cmd->IASetVertexBuffers(0, 2, views);
    cmd->IASetIndexBuffer(&mesh.ibView);
    cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);

    ++drawIndex_;
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
        !shadowPSO_ || drawIndex_ >= kMaxDraws) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    const XMMATRIX world = MakeWorldMatrix(transform);
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
    cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
    cmd->SetPipelineState(shadowPSO_.Get());
    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
    cmd->SetGraphicsRootConstantBufferView(2, materialCbAddr);
    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));
    cmd->IASetVertexBuffers(0, 1, &mesh.vbView);
    cmd->IASetIndexBuffer(&mesh.ibView);
    cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
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
        !instancedShadowPSO_ || !instances || instanceCount == 0 ||
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

    if (cmd == nullptr) {
        return;
    }
    cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
    cmd->SetPipelineState(instancedShadowPSO_.Get());
    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
    cmd->SetGraphicsRootConstantBufferView(2, materialCbAddr);
    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));

    D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, instanceView};
    cmd->IASetVertexBuffers(0, 2, views);
    cmd->IASetIndexBuffer(&mesh.ibView);
    cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);
    ++drawIndex_;
}

void MeshRenderer::DrawMeshInstancedShadowWithPipeline(
    uint32_t pipelineId, const Mesh &mesh, const Material &material,
    const InstanceData *instances, uint32_t instanceCount,
    const DirectX::XMFLOAT4X4 &lightViewProjection, uint32_t textureId) {
    if (!dxCommon_ || !textureManager_ || !shadowRootSignature_ ||
        pipelineId >= customInstancedPipelines_.size() || !instances ||
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

    if (cmd == nullptr ||
        !customInstancedPipelines_[pipelineId].shadowPipelineState) {
        return;
    }
    cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
    cmd->SetPipelineState(
        customInstancedPipelines_[pipelineId].shadowPipelineState.Get());
    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
    cmd->SetGraphicsRootConstantBufferView(2, materialCbAddr);
    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(textureManager_, drawMaterial,
                                         textureId)));

    D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, instanceView};
    cmd->IASetVertexBuffers(0, 2, views);
    cmd->IASetIndexBuffer(&mesh.ibView);
    cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);
    ++drawIndex_;
}



bool MeshRenderer::SetPipelineForMaterial(const Material &material) {
    auto *cmd = dxCommon_ ? dxCommon_->GetCommandList() : nullptr;
    ID3D12PipelineState *pipelineState =
        pipelineStates_[PipelineVariantIndex(material)].Get();
    if (cmd == nullptr || pipelineState == nullptr) {
        return false;
    }
    cmd->SetPipelineState(pipelineState);
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
    cmd->SetPipelineState(pipelineState);
    return true;
}

bool MeshRenderer::SetInstancedPipelineForMaterial(const Material &material) {
    auto *cmd = dxCommon_ ? dxCommon_->GetCommandList() : nullptr;
    ID3D12PipelineState *pipelineState =
        instancedPipelineStates_[PipelineVariantIndex(material)].Get();
    if (cmd == nullptr || pipelineState == nullptr) {
        return false;
    }
    cmd->SetPipelineState(pipelineState);
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
    cmd->SetPipelineState(pipelineState);
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
    shadowFilterParams_ = {ClampFiniteMin(settings.filterRadius, 0.0f),
                           ClampFiniteMin(settings.depthSoftness, 0.0001f),
                           ClampFiniteMin(settings.edgeFade, 0.0f), 0.0f};
}

void MeshRenderer::SetCustomSceneParams(const DirectX::XMFLOAT4 &params0,
                                        const DirectX::XMFLOAT4 &params1) {
    customSceneParams0_ = params0;
    customSceneParams1_ = params1;
}

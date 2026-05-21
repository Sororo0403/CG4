#include "model/MeshRenderer.h"

#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace DirectX;
using namespace DxUtils;
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

XMMATRIX MakeWorldMatrix(const Transform &transform) {
    XMVECTOR q = XMQuaternionNormalize(XMLoadFloat4(&transform.rotation));
    return XMMatrixScaling(transform.scale.x, transform.scale.y,
                           transform.scale.z) *
           XMMatrixRotationQuaternion(q) *
           XMMatrixTranslation(transform.position.x, transform.position.y,
                               transform.position.z);
}

XMMATRIX MakeWorldInverseTranspose(const XMMATRIX &world) {
    XMVECTOR determinant{};
    XMMATRIX inverse = XMMatrixInverse(&determinant, world);
    const float determinantValue = XMVectorGetX(determinant);
    if (!std::isfinite(determinantValue) ||
        std::abs(determinantValue) <= 0.000001f) {
        return XMMatrixIdentity();
    }
    return XMMatrixTranspose(inverse);
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

uint32_t ResolveNormalTextureId(TextureManager *textureManager,
                                uint32_t normalTextureId) {
    return normalTextureId == UINT32_MAX
               ? textureManager->GetDefaultNormalTextureId()
               : normalTextureId;
}

uint32_t ResolveBaseColorTextureId(const Material &material,
                                   uint32_t fallbackTextureId) {
    return material.baseColorTextureId == UINT32_MAX
               ? fallbackTextureId
               : material.baseColorTextureId;
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
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    textureManager_ = textureManager;

    CreateRootSignature();
    CreateShadowRootSignature();
    CreatePipelineStates();
    CreateShadowPipelineStates();
    CreateUploadBuffer();
}

void MeshRenderer::BeginFrame() {
    uploadBuffer_.BeginFrame();
}

void MeshRenderer::PreDraw() {
    auto *cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    drawIndex_ = 0;
}

void MeshRenderer::PostDraw() {}

void MeshRenderer::PreDrawShadow() {
    auto *cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
    drawIndex_ = 0;
}

void MeshRenderer::DrawMesh(const Mesh &mesh, const Material &material,
                            const Transform &transform, const Camera &camera,
                            uint32_t textureId, uint32_t normalTextureId) {
    if (drawIndex_ >= kMaxDraws) {
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

    SetPipelineForMaterial(drawMaterial);
    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
    cmd->SetGraphicsRootConstantBufferView(2, materialCbAddr);
    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(drawMaterial, textureId)));
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
    if (pipelineId >= customPipelines_.size() || drawIndex_ >= kMaxDraws) {
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

    SetPipelineForMaterial(customPipelines_[pipelineId].pipelineStates,
                           drawMaterial);
    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
    cmd->SetGraphicsRootConstantBufferView(2, materialCbAddr);
    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(drawMaterial, textureId)));
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

void MeshRenderer::DrawMeshInstanced(const Mesh &mesh, const Material &material,
                                     const InstanceData *instances,
                                     uint32_t instanceCount,
                                     const Camera &camera,
                                     uint32_t textureId,
                                     uint32_t normalTextureId) {
    if (!instances || instanceCount == 0 || drawIndex_ >= kMaxDraws) {
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

    SetInstancedPipelineForMaterial(drawMaterial);
    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
    cmd->SetGraphicsRootConstantBufferView(2, materialCbAddr);
    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(drawMaterial, textureId)));
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
    if (pipelineId >= customInstancedPipelines_.size() || !instances ||
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

    SetInstancedPipelineForMaterial(
        customInstancedPipelines_[pipelineId].pipelineStates, drawMaterial);
    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
    cmd->SetGraphicsRootConstantBufferView(2, materialCbAddr);
    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(drawMaterial, textureId)));
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
    if (drawIndex_ >= kMaxDraws) {
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
    cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
    cmd->SetPipelineState(shadowPSO_.Get());
    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
    cmd->SetGraphicsRootConstantBufferView(2, materialCbAddr);
    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(drawMaterial, textureId)));
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
    if (!instances || instanceCount == 0 || drawIndex_ >= kMaxDraws) {
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

    cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
    cmd->SetPipelineState(instancedShadowPSO_.Get());
    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
    cmd->SetGraphicsRootConstantBufferView(2, materialCbAddr);
    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(drawMaterial, textureId)));

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
    if (pipelineId >= customInstancedPipelines_.size() || !instances ||
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

    cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
    cmd->SetPipelineState(
        customInstancedPipelines_[pipelineId].shadowPipelineState.Get());
    cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
    cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
    cmd->SetGraphicsRootConstantBufferView(2, materialCbAddr);
    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(
               ResolveBaseColorTextureId(drawMaterial, textureId)));

    D3D12_VERTEX_BUFFER_VIEW views[] = {mesh.vbView, instanceView};
    cmd->IASetVertexBuffers(0, 2, views);
    cmd->IASetIndexBuffer(&mesh.ibView);
    cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
    cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);
    ++drawIndex_;
}



void MeshRenderer::SetPipelineForMaterial(const Material &material) {
    dxCommon_->GetCommandList()->SetPipelineState(
        pipelineStates_[PipelineVariantIndex(material)].Get());
}

void MeshRenderer::SetPipelineForMaterial(
    const std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
                     kPipelineVariantCount> &pipelineStates,
    const Material &material) {
    dxCommon_->GetCommandList()->SetPipelineState(
        pipelineStates[PipelineVariantIndex(material)].Get());
}

void MeshRenderer::SetInstancedPipelineForMaterial(const Material &material) {
    dxCommon_->GetCommandList()->SetPipelineState(
        instancedPipelineStates_[PipelineVariantIndex(material)].Get());
}

void MeshRenderer::SetInstancedPipelineForMaterial(
    const std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
                     kPipelineVariantCount> &pipelineStates,
    const Material &material) {
    dxCommon_->GetCommandList()->SetPipelineState(
        pipelineStates[PipelineVariantIndex(material)].Get());
}

void MeshRenderer::SetShadowMap(
    D3D12_GPU_DESCRIPTOR_HANDLE shadowMap,
    const DirectX::XMFLOAT4X4 &lightViewProjection,
    const SceneShadowSettings &settings) {
    shadowMapGpuHandle_ = shadowMap;
    shadowLightViewProjection_ = lightViewProjection;
    shadowParams_ = {1.0f, settings.bias,
                     (std::clamp)(settings.strength, 0.0f, 1.0f),
                     settings.normalBias};
    shadowFilterParams_ = {(std::max)(settings.filterRadius, 0.0f),
                           (std::max)(settings.depthSoftness, 0.0001f),
                           (std::max)(settings.edgeFade, 0.0f), 0.0f};
}

void MeshRenderer::SetCustomSceneParams(const DirectX::XMFLOAT4 &params0,
                                        const DirectX::XMFLOAT4 &params1) {
    customSceneParams0_ = params0;
    customSceneParams1_ = params1;
}
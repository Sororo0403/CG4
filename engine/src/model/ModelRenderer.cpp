#include "model/ModelRenderer.h"
#include "core/Numeric.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "model/MaterialManager.h"
#include "model/MeshManager.h"
#include "RendererMaterialUtils.h"
#include "model/RendererMath.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;
using GpuResourceHelpers::MapResourceChecked;
using Numeric::AtLeastFinite;
using Numeric::ClampFinite;
using Numeric::FiniteOr;
using RendererMaterialUtils::PipelineVariantIndex;
using RendererMaterialUtils::ResolveBaseColorTextureId;
using RendererMaterialUtils::ResolveNormalTextureId;

constexpr UINT kSkinningThreadCount = 1024u;

uint32_t ResolveEnvironmentTextureId(TextureManager *textureManager,
                                     uint32_t requestedTextureId,
                                     uint32_t configuredTextureId,
                                     bool hasConfiguredTexture) {
    if (textureManager == nullptr) {
        return UINT32_MAX;
    }

    const uint32_t selectedTextureId =
        requestedTextureId != UINT32_MAX
            ? requestedTextureId
            : (hasConfiguredTexture ? configuredTextureId : UINT32_MAX);
    if (selectedTextureId != UINT32_MAX &&
        textureManager->IsCubeTextureId(selectedTextureId)) {
        return selectedTextureId;
    }

    const uint32_t fallbackTextureId = textureManager->GetBlackCubeTextureId();
    return textureManager->IsCubeTextureId(fallbackTextureId)
               ? fallbackTextureId
               : UINT32_MAX;
}

bool IsDrawableSubMesh(const ModelSubMesh &subMesh, MeshManager *meshManager,
                       MaterialManager *materialManager) {
    return meshManager != nullptr && materialManager != nullptr &&
           meshManager->IsValidMeshId(subMesh.meshId) &&
           materialManager->IsValidMaterialId(subMesh.materialId);
}

bool HasCompleteSkinningDescriptors(const SkinCluster &skinCluster) {
    return skinCluster.inputVertexSrvGpuHandle.ptr != 0 &&
           skinCluster.influenceSrvGpuHandle.ptr != 0 &&
           skinCluster.skinnedVertexUavGpuHandle.ptr != 0;
}

D3D12_GPU_VIRTUAL_ADDRESS GetCurrentPaletteAddress(
    const SkinCluster &skinCluster, DirectXCommon *dxCommon) {
    if (skinCluster.paletteFrames.empty()) {
        return 0;
    }
    const size_t frameIndex =
        dxCommon != nullptr
            ? dxCommon->GetBackBufferIndex() % skinCluster.paletteFrames.size()
            : 0;
    if (frameIndex >= skinCluster.paletteFrames.size()) {
        return 0;
    }
    const SkinPaletteFrame &frame = skinCluster.paletteFrames[frameIndex];
    return frame.resource ? frame.resource->GetGPUVirtualAddress() : 0;
}

bool HasRenderableVertexSource(const ModelSubMesh &subMesh) {
    const SkinCluster &skinCluster = subMesh.skinCluster;
    if (!skinCluster.skinnedVertexResource) {
        return true;
    }

    return skinCluster.skinningValid &&
           HasCompleteSkinningDescriptors(skinCluster) &&
           skinCluster.skinnedVertexBufferView.BufferLocation != 0 &&
           skinCluster.skinnedVertexBufferView.SizeInBytes > 0 &&
           skinCluster.skinnedVertexBufferView.StrideInBytes > 0;
}

bool IsDrawableSubMeshWithValidVertexSource(
    const ModelSubMesh &subMesh, MeshManager *meshManager,
    MaterialManager *materialManager) {
    return IsDrawableSubMesh(subMesh, meshManager, materialManager) &&
           HasRenderableVertexSource(subMesh);
}

bool HasPaletteBuffer(const ModelSubMesh &subMesh, DirectXCommon *dxCommon,
                      D3D12_GPU_VIRTUAL_ADDRESS identityPaletteAddress) {
    const SkinCluster &skinCluster = subMesh.skinCluster;
    if (skinCluster.paletteCount > 0) {
        return GetCurrentPaletteAddress(skinCluster, dxCommon) != 0;
    }
    return identityPaletteAddress != 0;
}

D3D12_GPU_VIRTUAL_ADDRESS GetPaletteAddressForDraw(
    const SkinCluster &skinCluster, DirectXCommon *dxCommon,
    D3D12_GPU_VIRTUAL_ADDRESS identityPaletteAddress) {
    if (skinCluster.paletteCount > 0) {
        return GetCurrentPaletteAddress(skinCluster, dxCommon);
    }
    return identityPaletteAddress;
}

bool IsForwardDrawableSubMesh(const ModelSubMesh &subMesh,
                              MeshManager *meshManager,
                              MaterialManager *materialManager,
                              DirectXCommon *dxCommon,
                              D3D12_GPU_VIRTUAL_ADDRESS identityPaletteAddress) {
    return IsDrawableSubMesh(subMesh, meshManager, materialManager) &&
           HasPaletteBuffer(subMesh, dxCommon, identityPaletteAddress) &&
           HasRenderableVertexSource(subMesh);
}

}

static XMFLOAT4 ClampFiniteColor(const XMFLOAT4 &value,
                                 const XMFLOAT4 &fallback) {
    return {ClampFinite(value.x, 0.0f, 1.0f, fallback.x),
            ClampFinite(value.y, 0.0f, 1.0f, fallback.y),
            ClampFinite(value.z, 0.0f, 1.0f, fallback.z),
            ClampFinite(value.w, 0.0f, 1.0f, fallback.w)};
}

static ModelDrawEffect SanitizeDrawEffect(ModelDrawEffect effect) {
    const ModelDrawEffect defaults{};
    effect.color = ClampFiniteColor(effect.color, defaults.color);
    effect.intensity = AtLeastFinite(effect.intensity, 0.0f, 0.0f);
    effect.fresnelPower =
        AtLeastFinite(effect.fresnelPower, 0.5f, defaults.fresnelPower);
    effect.noiseAmount =
        ClampFinite(effect.noiseAmount, 0.0f, 1.0f, defaults.noiseAmount);
    effect.time = FiniteOr(effect.time, defaults.time);
    effect.baseDim = ClampFinite(effect.baseDim, 0.0f, 1.0f, defaults.baseDim);
    effect.alphaBoost =
        AtLeastFinite(effect.alphaBoost, 0.0f, defaults.alphaBoost);
    effect.surfaceTint =
        ClampFinite(effect.surfaceTint, 0.0f, 1.0f, defaults.surfaceTint);
    effect.alphaMultiplier = ClampFinite(effect.alphaMultiplier, 0.0f, 1.0f,
                                         defaults.alphaMultiplier);

    const uint32_t blendOverride = static_cast<uint32_t>(effect.blendOverride);
    if (blendOverride >
        static_cast<uint32_t>(ModelDrawEffectBlendOverride::Opaque)) {
        effect.blendOverride = ModelDrawEffectBlendOverride::KeepMaterial;
    }
    return effect;
}

static std::vector<uint8_t> CreateDissolveNoisePixels(uint32_t width,
                                                      uint32_t height) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t h = x * 374761393u ^ y * 668265263u ^ 0x8DA6B343u;
            h = (h ^ (h >> 13u)) * 1274126177u;
            h ^= h >> 16u;
            const uint8_t value = static_cast<uint8_t>(h & 0xFFu);
            const size_t index = (static_cast<size_t>(y) * width + x) * 4u;
            pixels[index + 0u] = value;
            pixels[index + 1u] = value;
            pixels[index + 2u] = value;
            pixels[index + 3u] = 255u;
        }
    }
    return pixels;
}

struct PerObjectConstBufferData {
    XMFLOAT4X4 matWVP;
    XMFLOAT4X4 matWorld;
    XMFLOAT4X4 matWorldInverseTranspose;
};

ModelRenderer::~ModelRenderer() {
    Finalize(true);
}

void ModelRenderer::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                               MeshManager *meshManager,
                               TextureManager *textureManager,
                               MaterialManager *materialManager) {
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager || !meshManager ||
        !textureManager || !materialManager) {
        Finalize();
        return;
    }

    if (!Finalize()) {
        return;
    }
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    meshManager_ = meshManager;
    textureManager_ = textureManager;
    materialManager_ = materialManager;
    environmentTextureId_ = textureManager_->GetWhiteCubeTextureId();
    hasEnvironmentTexture_ = true;
    dissolveNoiseTextureId_ = textureManager_->GetWhiteTextureId();
    shadowMapGpuHandle_ =
        textureManager_->GetGpuHandle(textureManager_->GetWhiteTextureId());
    const std::vector<uint8_t> dissolveNoise =
        CreateDissolveNoisePixels(128u, 128u);
    dissolveNoiseTextureId_ = textureManager_->CreateFromRgbaPixels(
        128u, 128u, dissolveNoise.data());

    CreateRootSignature();
    CreateShadowRootSignature();
    CreateSkinningRootSignature();
    CreatePipelineState();
    CreateShadowPipelineState();
    CreateSkinningPipelineState();
    CreateUploadBuffer();
    CreateIdentityPalette();
    if (!rootSignature_ || !shadowRootSignature_ || !skinningRootSignature_ ||
        !pipelineStates_[0] || !instancedPipelineStates_[0] || !shadowPSO_ ||
        !instancedShadowPSO_ || !skinningPSO_ ||
        uploadBuffer_.GetBytesPerFrame() == 0 ||
        GetIdentityPaletteAddress() == 0) {
        Finalize();
    }
}

bool ModelRenderer::Finalize() { return Finalize(false); }

bool ModelRenderer::Finalize(bool allowFrameAbort) {
    const bool hasGpuResources =
        rootSignature_ || shadowRootSignature_ || skinningRootSignature_ ||
        pipelineStates_[0] || instancedPipelineStates_[0] || shadowPSO_ ||
        instancedShadowPSO_ || skinningPSO_ ||
        HasIdentityPaletteResources() ||
        uploadBuffer_.GetBytesPerFrame() != 0;
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources,
                                allowFrameAbort)) {
        return false;
    }
    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }

    ResetResources();
    return true;
}

bool ModelRenderer::CreateIdentityPalette() {
    ResetIdentityPalette();
    if (dxCommon_ == nullptr || dxCommon_->GetDevice() == nullptr) {
        return false;
    }

    const UINT frameCount =
        (std::max)(1u, dxCommon_->GetSwapChainBufferCount());
    try {
        identityPaletteFrames_.resize(frameCount);
    } catch (...) {
        return false;
    }

    WellForGPU identity{};
    identity.skeletonSpaceMatrix =
        RendererMath::StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));
    identity.skeletonSpaceInverseTransposeMatrix =
        RendererMath::StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto paletteDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(WellForGPU));
    for (SkinPaletteFrame &frame : identityPaletteFrames_) {
        if (!CreateCommittedResourceChecked(
                dxCommon_->GetDevice(), &uploadHeap, D3D12_HEAP_FLAG_NONE,
                &paletteDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                frame.resource.GetAddressOf())) {
            ResetIdentityPalette();
            return false;
        }

        void *mapped = nullptr;
        if (!MapResourceChecked(frame.resource.Get(), &mapped)) {
            ResetIdentityPalette();
            return false;
        }
        std::memcpy(mapped, &identity, sizeof(identity));
        frame.resource->Unmap(0, nullptr);
        frame.mappedPalette = nullptr;
    }

    return GetIdentityPaletteAddress() != 0;
}

void ModelRenderer::ResetIdentityPalette() noexcept {
    for (SkinPaletteFrame &frame : identityPaletteFrames_) {
        if (frame.resource && frame.mappedPalette != nullptr) {
            frame.resource->Unmap(0, nullptr);
        }
        frame.mappedPalette = nullptr;
        frame.resource.Reset();
    }
    identityPaletteFrames_.clear();
}

bool ModelRenderer::HasIdentityPaletteResources() const noexcept {
    for (const SkinPaletteFrame &frame : identityPaletteFrames_) {
        if (frame.resource) {
            return true;
        }
    }
    return false;
}

D3D12_GPU_VIRTUAL_ADDRESS ModelRenderer::GetIdentityPaletteAddress() const {
    if (identityPaletteFrames_.empty()) {
        return 0;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr
            ? dxCommon_->GetBackBufferIndex() % identityPaletteFrames_.size()
            : 0;
    if (frameIndex >= identityPaletteFrames_.size()) {
        return 0;
    }
    const SkinPaletteFrame &frame = identityPaletteFrames_[frameIndex];
    return frame.resource ? frame.resource->GetGPUVirtualAddress() : 0;
}

void ModelRenderer::ResetResources() {
    ResetIdentityPalette();
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    meshManager_ = nullptr;
    textureManager_ = nullptr;
    materialManager_ = nullptr;
    rootSignature_.Reset();
    shadowRootSignature_.Reset();
    skinningRootSignature_.Reset();
    for (auto &pipeline : pipelineStates_) {
        pipeline.Reset();
    }
    for (auto &pipeline : instancedPipelineStates_) {
        pipeline.Reset();
    }
    shadowPSO_.Reset();
    instancedShadowPSO_.Reset();
    skinningPSO_.Reset();
    uploadBuffer_.Reset();
    drawIndex_ = 0;
    currentGraphicsRootSignature_ = nullptr;
    currentGraphicsPipelineState_ = nullptr;
    hasEnvironmentTexture_ = false;
    shadowMapGpuHandle_ = {};
}

bool ModelRenderer::IsReady() const {
    const auto hasAllPipelineStates = [](const auto &pipelines) {
        for (const auto &pipeline : pipelines) {
            if (!pipeline) {
                return false;
            }
        }
        return true;
    };

    return dxCommon_ != nullptr && srvManager_ != nullptr &&
           meshManager_ != nullptr && textureManager_ != nullptr &&
            materialManager_ != nullptr && rootSignature_ &&
            shadowRootSignature_ && skinningRootSignature_ &&
            hasAllPipelineStates(pipelineStates_) &&
            hasAllPipelineStates(instancedPipelineStates_) && shadowPSO_ &&
            instancedShadowPSO_ && skinningPSO_ &&
            GetIdentityPaletteAddress() != 0 &&
            uploadBuffer_.GetBytesPerFrame() != 0;
}

void ModelRenderer::SetDrawEffect(const ModelDrawEffect &effect) {
    currentEffect_ = SanitizeDrawEffect(effect);
}

void ModelRenderer::BeginFrame() {
    if (!dxCommon_) {
        drawIndex_ = 0;
        return;
    }
    uploadBuffer_.BeginFrame(dxCommon_->GetBackBufferIndex());
    drawIndex_ = 0;
    ++skinningFrameId_;
    if (skinningFrameId_ == 0) {
        skinningFrameId_ = 1;
    }
}

void ModelRenderer::PreDraw() {
    if (!dxCommon_ || !srvManager_ || !rootSignature_) {
        currentGraphicsRootSignature_ = nullptr;
        currentGraphicsPipelineState_ = nullptr;
        drawIndex_ = 0;
        return;
    }
    auto cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    if (cmd == nullptr || heap == nullptr) {
        currentGraphicsRootSignature_ = nullptr;
        currentGraphicsPipelineState_ = nullptr;
        drawIndex_ = 0;
        return;
    }

    ID3D12DescriptorHeap *heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);

    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    currentGraphicsRootSignature_ = rootSignature_.Get();
    currentGraphicsPipelineState_ = nullptr;

    drawIndex_ = 0;
}

void ModelRenderer::Draw(const Model &model, const Transform &transform,
                         const Camera &camera, uint32_t environmentTextureId) {
    if (!dxCommon_ || !meshManager_ || !textureManager_ ||
        !materialManager_ || !rootSignature_ || drawIndex_ >= kMaxDraws) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        return;
    }

    XMMATRIX world = RendererMath::MakeWorldMatrix(transform);

    if (model.hasRootAnimation) {
        world = XMLoadFloat4x4(&model.rootAnimationMatrix) * world;
    }

    XMMATRIX worldInverseTranspose =
        RendererMath::MakeSafeInverseTranspose(world);

    XMMATRIX wvp = world * camera.GetView() * camera.GetProj();
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(wvp, world, worldInverseTranspose);
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS effectCbAddr =
        WriteDrawEffectConstants();
    if (objectCbAddr == 0 || sceneCbAddr == 0 || effectCbAddr == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    const D3D12_GPU_VIRTUAL_ADDRESS identityPaletteAddress =
        GetIdentityPaletteAddress();
    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }
        if (!IsForwardDrawableSubMesh(subMesh, meshManager_,
                                      materialManager_, dxCommon_,
                                      identityPaletteAddress)) {
            return;
        }

        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);

        if (!SetPipelineForMaterial(material)) {
            return;
        }

        const Mesh &mesh = meshManager_->GetMesh(subMesh.meshId);
        const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
            subMesh.skinCluster.skinnedVertexResource
                ? subMesh.skinCluster.skinnedVertexBufferView
                : mesh.vbView;
        const D3D12_GPU_VIRTUAL_ADDRESS paletteAddress =
            GetPaletteAddressForDraw(subMesh.skinCluster, dxCommon_,
                                     identityPaletteAddress);
        if (paletteAddress == 0) {
            return;
        }

        cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
        cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
        cmd->SetGraphicsRootConstantBufferView(
            2, materialManager_->GetGPUVirtualAddress(subMesh.materialId));
        cmd->SetGraphicsRootDescriptorTable(
            3, textureManager_->GetGpuHandle(
                   ResolveBaseColorTextureId(textureManager_, material,
                                             subMesh.textureId)));
        cmd->SetGraphicsRootShaderResourceView(4, paletteAddress);
        const uint32_t safeEnvironmentTextureId =
            ResolveEnvironmentTextureId(textureManager_, environmentTextureId,
                                        environmentTextureId_,
                                        hasEnvironmentTexture_);
        if (safeEnvironmentTextureId == UINT32_MAX) {
            return;
        }
        cmd->SetGraphicsRootDescriptorTable(
            5, textureManager_->GetGpuHandle(safeEnvironmentTextureId));
        cmd->SetGraphicsRootDescriptorTable(6, shadowMapGpuHandle_);
        cmd->SetGraphicsRootDescriptorTable(
            7, textureManager_->GetGpuHandle(ResolveNormalTextureId(
                   textureManager_, material, subMesh.normalTextureId)));
        cmd->SetGraphicsRootConstantBufferView(8, effectCbAddr);
        cmd->SetGraphicsRootDescriptorTable(
            9, textureManager_->GetGpuHandle(dissolveNoiseTextureId_));

        cmd->IASetVertexBuffers(0, 1, &vertexBufferView);
        cmd->IASetIndexBuffer(&mesh.ibView);
        cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
        cmd->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);

        drawIndex_++;
    };

    if (!model.subMeshes.empty()) {
        for (const auto &subMesh : model.subMeshes) {
            drawSubMesh(subMesh);
            if (drawIndex_ >= kMaxDraws) {
                break;
            }
        }
    }
}

void ModelRenderer::DrawInstanced(const Model &model,
                                  const Transform *transforms,
                                  uint32_t instanceCount,
                                  const Camera &camera,
                                  uint32_t environmentTextureId) {
    if (!dxCommon_ || !meshManager_ || !textureManager_ ||
        !materialManager_ || !rootSignature_ || !transforms ||
        instanceCount == 0) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        return;
    }

    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(),
                             XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS effectCbAddr =
        WriteDrawEffectConstants();
    const D3D12_VERTEX_BUFFER_VIEW instanceView =
        WriteInstances(model, transforms, instanceCount);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || effectCbAddr == 0 ||
        instanceView.BufferLocation == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    const D3D12_GPU_VIRTUAL_ADDRESS identityPaletteAddress =
        GetIdentityPaletteAddress();
    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }
        if (!IsForwardDrawableSubMesh(subMesh, meshManager_,
                                      materialManager_, dxCommon_,
                                      identityPaletteAddress)) {
            return;
        }

        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);
        if (!SetInstancedPipelineForMaterial(material)) {
            return;
        }

        const Mesh &mesh = meshManager_->GetMesh(subMesh.meshId);
        const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
            subMesh.skinCluster.skinnedVertexResource
                ? subMesh.skinCluster.skinnedVertexBufferView
                : mesh.vbView;
        const D3D12_GPU_VIRTUAL_ADDRESS paletteAddress =
            GetPaletteAddressForDraw(subMesh.skinCluster, dxCommon_,
                                     identityPaletteAddress);
        if (paletteAddress == 0) {
            return;
        }
        D3D12_VERTEX_BUFFER_VIEW views[] = {vertexBufferView, instanceView};

        cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
        cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
        cmd->SetGraphicsRootConstantBufferView(
            2, materialManager_->GetGPUVirtualAddress(subMesh.materialId));
        cmd->SetGraphicsRootDescriptorTable(
            3, textureManager_->GetGpuHandle(
                   ResolveBaseColorTextureId(textureManager_, material,
                                             subMesh.textureId)));
        cmd->SetGraphicsRootShaderResourceView(4, paletteAddress);

        const uint32_t safeEnvironmentTextureId =
            ResolveEnvironmentTextureId(textureManager_, environmentTextureId,
                                        environmentTextureId_,
                                        hasEnvironmentTexture_);
        if (safeEnvironmentTextureId == UINT32_MAX) {
            return;
        }
        cmd->SetGraphicsRootDescriptorTable(
            5, textureManager_->GetGpuHandle(safeEnvironmentTextureId));
        cmd->SetGraphicsRootDescriptorTable(6, shadowMapGpuHandle_);
        cmd->SetGraphicsRootDescriptorTable(
            7, textureManager_->GetGpuHandle(ResolveNormalTextureId(
                   textureManager_, material, subMesh.normalTextureId)));
        cmd->SetGraphicsRootConstantBufferView(8, effectCbAddr);
        cmd->SetGraphicsRootDescriptorTable(
            9, textureManager_->GetGpuHandle(dissolveNoiseTextureId_));

        cmd->IASetVertexBuffers(0, 2, views);
        cmd->IASetIndexBuffer(&mesh.ibView);
        cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
        cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);

        ++drawIndex_;
    };

    for (const auto &subMesh : model.subMeshes) {
        drawSubMesh(subMesh);
        if (drawIndex_ >= kMaxDraws) {
            break;
        }
    }
}

void ModelRenderer::DrawInstanced(const Model &model,
                                  const InstanceData *instances,
                                  uint32_t instanceCount,
                                  const Camera &camera,
                                  uint32_t environmentTextureId) {
    if (!dxCommon_ || !meshManager_ || !textureManager_ ||
        !materialManager_ || !rootSignature_ || !instances ||
        instanceCount == 0) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        return;
    }

    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(),
                             XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS effectCbAddr =
        WriteDrawEffectConstants();
    const D3D12_VERTEX_BUFFER_VIEW instanceView =
        WriteInstances(model, instances, instanceCount);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || effectCbAddr == 0 ||
        instanceView.BufferLocation == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    const D3D12_GPU_VIRTUAL_ADDRESS identityPaletteAddress =
        GetIdentityPaletteAddress();
    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }
        if (!IsForwardDrawableSubMesh(subMesh, meshManager_,
                                      materialManager_, dxCommon_,
                                      identityPaletteAddress)) {
            return;
        }

        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);
        if (!SetInstancedPipelineForMaterial(material)) {
            return;
        }

        const Mesh &mesh = meshManager_->GetMesh(subMesh.meshId);
        const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
            subMesh.skinCluster.skinnedVertexResource
                ? subMesh.skinCluster.skinnedVertexBufferView
                : mesh.vbView;
        const D3D12_GPU_VIRTUAL_ADDRESS paletteAddress =
            GetPaletteAddressForDraw(subMesh.skinCluster, dxCommon_,
                                     identityPaletteAddress);
        if (paletteAddress == 0) {
            return;
        }
        D3D12_VERTEX_BUFFER_VIEW views[] = {vertexBufferView, instanceView};

        cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
        cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
        cmd->SetGraphicsRootConstantBufferView(
            2, materialManager_->GetGPUVirtualAddress(subMesh.materialId));
        cmd->SetGraphicsRootDescriptorTable(
            3, textureManager_->GetGpuHandle(
                   ResolveBaseColorTextureId(textureManager_, material,
                                             subMesh.textureId)));
        cmd->SetGraphicsRootShaderResourceView(4, paletteAddress);

        const uint32_t safeEnvironmentTextureId =
            ResolveEnvironmentTextureId(textureManager_, environmentTextureId,
                                        environmentTextureId_,
                                        hasEnvironmentTexture_);
        if (safeEnvironmentTextureId == UINT32_MAX) {
            return;
        }
        cmd->SetGraphicsRootDescriptorTable(
            5, textureManager_->GetGpuHandle(safeEnvironmentTextureId));
        cmd->SetGraphicsRootDescriptorTable(6, shadowMapGpuHandle_);
        cmd->SetGraphicsRootDescriptorTable(
            7, textureManager_->GetGpuHandle(ResolveNormalTextureId(
                   textureManager_, material, subMesh.normalTextureId)));
        cmd->SetGraphicsRootConstantBufferView(8, effectCbAddr);
        cmd->SetGraphicsRootDescriptorTable(
            9, textureManager_->GetGpuHandle(dissolveNoiseTextureId_));

        cmd->IASetVertexBuffers(0, 2, views);
        cmd->IASetIndexBuffer(&mesh.ibView);
        cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
        cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);

        ++drawIndex_;
    };

    for (const auto &subMesh : model.subMeshes) {
        drawSubMesh(subMesh);
        if (drawIndex_ >= kMaxDraws) {
            break;
        }
    }
}

void ModelRenderer::PostDraw() {}

void ModelRenderer::SetShadowMap(
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
    shadowFilterParams_ = {
        AtLeastFinite(settings.filterRadius, 0.0f, 0.0f),
        AtLeastFinite(settings.depthSoftness, 0.0001f, 0.0001f),
        AtLeastFinite(settings.edgeFade, 0.0f, 0.0f), 0.0f};
}

void ModelRenderer::PreDrawShadow() {
    if (!dxCommon_ || !srvManager_ || !shadowRootSignature_ || !shadowPSO_) {
        currentGraphicsRootSignature_ = nullptr;
        currentGraphicsPipelineState_ = nullptr;
        return;
    }
    auto cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    if (cmd == nullptr || heap == nullptr) {
        currentGraphicsRootSignature_ = nullptr;
        currentGraphicsPipelineState_ = nullptr;
        return;
    }
    ID3D12DescriptorHeap *heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
    cmd->SetPipelineState(shadowPSO_.Get());
    currentGraphicsRootSignature_ = shadowRootSignature_.Get();
    currentGraphicsPipelineState_ = shadowPSO_.Get();
}

void ModelRenderer::DrawShadow(
    const Model &model, const Transform &transform,
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
    if (!dxCommon_ || !meshManager_ || !textureManager_ ||
        !materialManager_ || !shadowRootSignature_ || !shadowPSO_ ||
        drawIndex_ >= kMaxDraws) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        return;
    }
    XMMATRIX world = RendererMath::MakeWorldMatrix(transform);

    if (model.hasRootAnimation) {
        world = XMLoadFloat4x4(&model.rootAnimationMatrix) * world;
    }

    const XMMATRIX lightVP = XMLoadFloat4x4(&lightViewProjection);
    const XMMATRIX wvp = world * lightVP;
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(wvp, world, XMMatrixIdentity());
    if (objectCbAddr == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }
        if (!IsDrawableSubMeshWithValidVertexSource(
                subMesh, meshManager_, materialManager_)) {
            return;
        }

        cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
        cmd->SetPipelineState(shadowPSO_.Get());
        const Mesh &mesh = meshManager_->GetMesh(subMesh.meshId);
        const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
            subMesh.skinCluster.skinnedVertexResource
                ? subMesh.skinCluster.skinnedVertexBufferView
                : mesh.vbView;

        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);
        const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
            materialManager_->GetGPUVirtualAddress(subMesh.materialId);
        cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
        cmd->SetGraphicsRootConstantBufferView(1, materialCbAddr);
        cmd->SetGraphicsRootDescriptorTable(
            2, textureManager_->GetGpuHandle(
                   ResolveBaseColorTextureId(textureManager_, material,
                                             subMesh.textureId)));
        cmd->IASetVertexBuffers(0, 1, &vertexBufferView);
        cmd->IASetIndexBuffer(&mesh.ibView);
        cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
        cmd->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
        ++drawIndex_;
    };

    for (const auto &subMesh : model.subMeshes) {
        drawSubMesh(subMesh);
        if (drawIndex_ >= kMaxDraws) {
            break;
        }
    }
}

void ModelRenderer::DrawInstancedShadow(
    const Model &model, const Transform *transforms, uint32_t instanceCount,
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
    if (!dxCommon_ || !meshManager_ || !textureManager_ ||
        !materialManager_ || !shadowRootSignature_ || !instancedShadowPSO_ ||
        !transforms || instanceCount == 0 || drawIndex_ >= kMaxDraws) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        return;
    }
    const XMMATRIX lightVP = XMLoadFloat4x4(&lightViewProjection);
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(lightVP, XMMatrixIdentity(), XMMatrixIdentity());
    const D3D12_VERTEX_BUFFER_VIEW instanceView =
        WriteInstances(model, transforms, instanceCount);
    if (objectCbAddr == 0 || instanceView.BufferLocation == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }
        if (!IsDrawableSubMeshWithValidVertexSource(
                subMesh, meshManager_, materialManager_)) {
            return;
        }

        cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
        cmd->SetPipelineState(instancedShadowPSO_.Get());
        const Mesh &mesh = meshManager_->GetMesh(subMesh.meshId);
        const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
            subMesh.skinCluster.skinnedVertexResource
                ? subMesh.skinCluster.skinnedVertexBufferView
                : mesh.vbView;
        D3D12_VERTEX_BUFFER_VIEW views[] = {vertexBufferView, instanceView};

        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);
        const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
            materialManager_->GetGPUVirtualAddress(subMesh.materialId);
        cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
        cmd->SetGraphicsRootConstantBufferView(1, materialCbAddr);
        cmd->SetGraphicsRootDescriptorTable(
            2, textureManager_->GetGpuHandle(
                   ResolveBaseColorTextureId(textureManager_, material,
                                             subMesh.textureId)));
        cmd->IASetVertexBuffers(0, 2, views);
        cmd->IASetIndexBuffer(&mesh.ibView);
        cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
        cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);
        ++drawIndex_;
    };

    for (const auto &subMesh : model.subMeshes) {
        drawSubMesh(subMesh);
        if (drawIndex_ >= kMaxDraws) {
            break;
        }
    }
}

void ModelRenderer::DrawInstancedShadow(
    const Model &model, const InstanceData *instances, uint32_t instanceCount,
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
    if (!dxCommon_ || !meshManager_ || !textureManager_ ||
        !materialManager_ || !shadowRootSignature_ || !instancedShadowPSO_ ||
        !instances || instanceCount == 0 || drawIndex_ >= kMaxDraws) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        return;
    }
    const XMMATRIX lightVP = XMLoadFloat4x4(&lightViewProjection);
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(lightVP, XMMatrixIdentity(), XMMatrixIdentity());
    const D3D12_VERTEX_BUFFER_VIEW instanceView =
        WriteInstances(model, instances, instanceCount);
    if (objectCbAddr == 0 || instanceView.BufferLocation == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }
        if (!IsDrawableSubMeshWithValidVertexSource(
                subMesh, meshManager_, materialManager_)) {
            return;
        }

        cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
        cmd->SetPipelineState(instancedShadowPSO_.Get());
        const Mesh &mesh = meshManager_->GetMesh(subMesh.meshId);
        const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
            subMesh.skinCluster.skinnedVertexResource
                ? subMesh.skinCluster.skinnedVertexBufferView
                : mesh.vbView;
        D3D12_VERTEX_BUFFER_VIEW views[] = {vertexBufferView, instanceView};

        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);
        const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
            materialManager_->GetGPUVirtualAddress(subMesh.materialId);
        cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
        cmd->SetGraphicsRootConstantBufferView(1, materialCbAddr);
        cmd->SetGraphicsRootDescriptorTable(
            2, textureManager_->GetGpuHandle(
                   ResolveBaseColorTextureId(textureManager_, material,
                                             subMesh.textureId)));
        cmd->IASetVertexBuffers(0, 2, views);
        cmd->IASetIndexBuffer(&mesh.ibView);
        cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
        cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);
        ++drawIndex_;
    };

    for (const auto &subMesh : model.subMeshes) {
        drawSubMesh(subMesh);
        if (drawIndex_ >= kMaxDraws) {
            break;
        }
    }
}






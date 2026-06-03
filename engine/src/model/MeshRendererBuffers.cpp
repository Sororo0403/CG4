#include "model/MeshRenderer.h"

#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "model/RendererMath.h"
#include "model/RendererSceneConstants.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;
using GpuResourceHelpers::MapResourceChecked;

struct PerObjectConstBufferData {
    XMFLOAT4X4 matWVP;
    XMFLOAT4X4 matWorld;
    XMFLOAT4X4 matWorldInverseTranspose;
};

constexpr size_t kMaxStaticInstanceBufferBytes = 64ull * 1024ull * 1024ull;

bool CanStageInstanceData(size_t bytesPerFrame, uint32_t instanceCount) {
    if (bytesPerFrame == 0 || instanceCount == 0) {
        return false;
    }
    if (instanceCount >
        (std::numeric_limits<size_t>::max)() / sizeof(InstanceData)) {
        return false;
    }
    return sizeof(InstanceData) * static_cast<size_t>(instanceCount) <=
           bytesPerFrame;
}

bool CanCreateStaticInstanceBuffer(uint32_t instanceCount) {
    if (instanceCount == 0) {
        return false;
    }
    if (instanceCount >
        (std::numeric_limits<size_t>::max)() / sizeof(InstanceData)) {
        return false;
    }
    return sizeof(InstanceData) * static_cast<size_t>(instanceCount) <=
           kMaxStaticInstanceBufferBytes;
}

uint64_t HashBytes(const void *data, size_t size) {
    constexpr uint64_t kFnvOffset = 14695981039346656037ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    const auto *bytes = static_cast<const uint8_t *>(data);
    uint64_t hash = kFnvOffset;
    for (size_t index = 0; index < size; ++index) {
        hash ^= static_cast<uint64_t>(bytes[index]);
        hash *= kFnvPrime;
    }
    return hash;
}

bool IsTransparentMaterial(const Material &material) {
    return material.blendMode == static_cast<int32_t>(BlendMode::Transparent) ||
           material.color.w < 1.0f;
}

bool AllocateDescriptor(SrvManager *srvManager, uint32_t &index,
                        D3D12_CPU_DESCRIPTOR_HANDLE &cpuHandle,
                        D3D12_GPU_DESCRIPTOR_HANDLE &gpuHandle) {
    index = UINT32_MAX;
    cpuHandle = {};
    gpuHandle = {};
    if (srvManager == nullptr || !srvManager->CanAllocate()) {
        return false;
    }
    index = srvManager->Allocate();
    if (index == UINT32_MAX) {
        return false;
    }
    cpuHandle = srvManager->GetCpuHandle(index);
    gpuHandle = srvManager->GetGpuHandle(index);
    if (cpuHandle.ptr == 0 || gpuHandle.ptr == 0) {
        srvManager->FreeIfAllocated(index);
        index = UINT32_MAX;
        cpuHandle = {};
        gpuHandle = {};
        return false;
    }
    return true;
}

bool HasGpuCullResources(const MeshGpuCullBuffer &buffer) noexcept {
    return buffer.outputResource || buffer.countResource ||
           buffer.drawArgsResource;
}

bool HasGpuLodCullResources(const MeshGpuLodCullBuffer &buffer) noexcept {
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        if (buffer.outputResources[lod] || buffer.countResources[lod] ||
            buffer.drawArgsResources[lod]) {
            return true;
        }
    }
    return false;
}

bool CanReleaseGpuCullResources(DirectXCommon *dxCommon,
                                bool hasResources,
                                bool allowFrameAbort) noexcept {
    return CanReleaseGpuResources(dxCommon, hasResources, allowFrameAbort);
}

void ResetGpuCullDescriptorsAndResources(SrvManager *srvManager,
                                         MeshGpuCullBuffer &buffer) noexcept {
    if (srvManager != nullptr) {
        srvManager->FreeIfAllocated(buffer.sourceSrvIndex);
        srvManager->FreeIfAllocated(buffer.outputUavIndex);
        srvManager->FreeIfAllocated(buffer.countUavIndex);
        srvManager->FreeIfAllocated(buffer.drawArgsUavIndex);
    }
    buffer.sourceSrvIndex = UINT32_MAX;
    buffer.outputUavIndex = UINT32_MAX;
    buffer.countUavIndex = UINT32_MAX;
    buffer.drawArgsUavIndex = UINT32_MAX;
    buffer.sourceSrvCpuHandle = {};
    buffer.sourceSrvGpuHandle = {};
    buffer.outputUavCpuHandle = {};
    buffer.outputUavGpuHandle = {};
    buffer.countUavCpuHandle = {};
    buffer.countUavGpuHandle = {};
    buffer.drawArgsUavCpuHandle = {};
    buffer.drawArgsUavGpuHandle = {};
    buffer.ResetResourcesOnly();
}

void ResetGpuLodCullDescriptorsAndResources(
    SrvManager *srvManager, MeshGpuLodCullBuffer &buffer) noexcept {
    if (srvManager != nullptr) {
        srvManager->FreeIfAllocated(buffer.sourceSrvIndex);
        for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
            srvManager->FreeIfAllocated(buffer.outputUavIndices[lod]);
            srvManager->FreeIfAllocated(buffer.countUavIndices[lod]);
            srvManager->FreeIfAllocated(buffer.drawArgsUavIndices[lod]);
        }
    }
    buffer.sourceSrvIndex = UINT32_MAX;
    buffer.outputUavIndices = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
    buffer.countUavIndices = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
    buffer.drawArgsUavIndices = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
    buffer.sourceSrvCpuHandle = {};
    buffer.sourceSrvGpuHandle = {};
    buffer.outputUavCpuHandles = {};
    buffer.outputUavGpuHandles = {};
    buffer.countUavCpuHandles = {};
    buffer.countUavGpuHandles = {};
    buffer.drawArgsUavCpuHandles = {};
    buffer.drawArgsUavGpuHandles = {};
    buffer.ResetResourcesOnly();
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

void MeshRenderer::CreateUploadBuffer() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        uploadBuffer_.Reset();
        return;
    }
    uploadBuffer_.Initialize(dxCommon_->GetDevice(), kUploadBytesPerFrame, 2);
}

D3D12_GPU_VIRTUAL_ADDRESS MeshRenderer::WriteObjectConstants(
    const XMMATRIX &wvp, const XMMATRIX &world,
    const XMMATRIX &worldInverseTranspose) {
    PerObjectConstBufferData data{};
    XMStoreFloat4x4(&data.matWVP, XMMatrixTranspose(wvp));
    XMStoreFloat4x4(&data.matWorld, XMMatrixTranspose(world));
    XMStoreFloat4x4(&data.matWorldInverseTranspose,
                    XMMatrixTranspose(worldInverseTranspose));
    return uploadBuffer_.Write(data).gpu;
}

D3D12_GPU_VIRTUAL_ADDRESS
MeshRenderer::WriteSceneConstants(const Camera &camera) {
    MeshSceneConstBufferData data{};
    auto *sceneDst = &data;
    sceneDst->cameraPos = {camera.GetPosition().x, camera.GetPosition().y,
                           camera.GetPosition().z, 1.0f};
    sceneDst->keyLightDirection = {currentLighting_.keyLightDirection.x,
                                   currentLighting_.keyLightDirection.y,
                                   currentLighting_.keyLightDirection.z, 0.0f};
    sceneDst->keyLightColor = currentLighting_.keyLightColor;
    sceneDst->fillLightDirection = {currentLighting_.fillLightDirection.x,
                                    currentLighting_.fillLightDirection.y,
                                    currentLighting_.fillLightDirection.z,
                                    0.0f};
    sceneDst->fillLightColor = currentLighting_.fillLightColor;
    sceneDst->ambientColor = currentLighting_.ambientColor;
    for (size_t lightIndex = 0; lightIndex < currentLighting_.pointLights.size();
         ++lightIndex) {
        sceneDst->pointLights[lightIndex].positionRange =
            currentLighting_.pointLights[lightIndex].positionRange;
        sceneDst->pointLights[lightIndex].colorIntensity =
            currentLighting_.pointLights[lightIndex].colorIntensity;
    }
    sceneDst->lightingParams = currentLighting_.lightingParams;
    sceneDst->fogColor = currentFog_.color;
    sceneDst->fogParams = currentFog_.params;
    XMStoreFloat4x4(&sceneDst->viewProjection,
                    XMMatrixTranspose(camera.GetView() * camera.GetProj()));
    XMStoreFloat4x4(
        &sceneDst->lightViewProjection,
        XMMatrixTranspose(XMLoadFloat4x4(&shadowLightViewProjection_)));
    sceneDst->shadowParams = shadowParams_;
    sceneDst->shadowFilterParams = shadowFilterParams_;
    sceneDst->customSceneParams0 = customSceneParams0_;
    sceneDst->customSceneParams1 = customSceneParams1_;
    sceneDst->spotLight.positionRange = currentLighting_.spotLight.positionRange;
    sceneDst->spotLight.direction = currentLighting_.spotLight.direction;
    sceneDst->spotLight.colorIntensity = currentLighting_.spotLight.colorIntensity;
    sceneDst->spotLight.angleParams = currentLighting_.spotLight.angleParams;
    const uint64_t hash = HashBytes(&data, sizeof(data));
    if (sceneConstantsCache_.valid && sceneConstantsCache_.hash == hash) {
        return sceneConstantsCache_.gpu;
    }
    const UploadAllocation allocation = uploadBuffer_.Write(data);
    if (allocation.gpu != 0) {
        sceneConstantsCache_.hash = hash;
        sceneConstantsCache_.gpu = allocation.gpu;
        sceneConstantsCache_.valid = true;
    }
    return allocation.gpu;
}

D3D12_GPU_VIRTUAL_ADDRESS
MeshRenderer::WriteShadowSceneConstants(
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
    MeshSceneConstBufferData data{};
    XMStoreFloat4x4(&data.viewProjection,
                    XMMatrixTranspose(XMLoadFloat4x4(&lightViewProjection)));
    data.customSceneParams0 = customSceneParams0_;
    data.customSceneParams1 = customSceneParams1_;
    const uint64_t hash = HashBytes(&data, sizeof(data));
    if (shadowSceneConstantsCache_.valid &&
        shadowSceneConstantsCache_.hash == hash) {
        return shadowSceneConstantsCache_.gpu;
    }
    const UploadAllocation allocation = uploadBuffer_.Write(data);
    if (allocation.gpu != 0) {
        shadowSceneConstantsCache_.hash = hash;
        shadowSceneConstantsCache_.gpu = allocation.gpu;
        shadowSceneConstantsCache_.valid = true;
    }
    return allocation.gpu;
}

D3D12_GPU_VIRTUAL_ADDRESS
MeshRenderer::WriteMaterialConstants(const Material &material) {
    const uint64_t hash = HashBytes(&material, sizeof(material));
    if (materialConstantsCache_.valid &&
        materialConstantsCache_.hash == hash) {
        return materialConstantsCache_.gpu;
    }
    const UploadAllocation allocation = uploadBuffer_.Write(material);
    if (allocation.gpu != 0) {
        materialConstantsCache_.hash = hash;
        materialConstantsCache_.gpu = allocation.gpu;
        materialConstantsCache_.valid = true;
    }
    return allocation.gpu;
}

D3D12_VERTEX_BUFFER_VIEW
MeshRenderer::WriteInstances(const InstanceData *instances,
                             uint32_t instanceCount) {
    if (!CanStageInstanceData(uploadBuffer_.GetBytesPerFrame(),
                              instanceCount)) {
        return {};
    }

    try {
        instanceScratch_.resize(instanceCount);
    } catch (...) {
        return {};
    }
    for (uint32_t index = 0; index < instanceCount; ++index) {
        instanceScratch_[index] = SanitizeInstanceDataForDraw(instances[index]);
    }

    const UploadAllocation allocation =
        uploadBuffer_.WriteArray(instanceScratch_.data(),
                                 instanceScratch_.size(),
                                 alignof(InstanceData));
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = allocation.gpu;
    view.SizeInBytes = static_cast<UINT>(allocation.size);
    view.StrideInBytes = sizeof(InstanceData);
    return view;
}

bool MeshRenderer::CreateStaticInstanceBuffer(const InstanceData *instances,
                                              uint32_t instanceCount,
                                              MeshInstanceBuffer &buffer) {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return false;
    }
    if (instanceCount == 0u || instances == nullptr) {
        return ReleaseStaticInstanceBuffer(buffer);
    }
    if (dxCommon_->IsCommandListRecording()) {
        return false;
    }
    if (!CanCreateStaticInstanceBuffer(instanceCount) ||
        instanceCount >
            (std::numeric_limits<UINT>::max)() / sizeof(InstanceData)) {
        return false;
    }

    try {
        instanceScratch_.resize(instanceCount);
    } catch (...) {
        return false;
    }
    for (uint32_t index = 0; index < instanceCount; ++index) {
        instanceScratch_[index] = SanitizeInstanceDataForDraw(instances[index]);
    }
    const UINT bufferSize =
        static_cast<UINT>(sizeof(InstanceData) * instanceScratch_.size());
    const uint64_t contentHash =
        HashBytes(instanceScratch_.data(), bufferSize);
    if (buffer.IsValid() && buffer.instanceCount == instanceCount &&
        buffer.contentHash == contentHash) {
        return true;
    }
    if (buffer.resource && !dxCommon_->IsDeviceRemoved()) {
        if (!dxCommon_->WaitForGpuIfPossible()) {
            return false;
        }
    }

    MeshInstanceBuffer nextBuffer{};
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

    if (!CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &defaultHeap, D3D12_HEAP_FLAG_NONE,
            &resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST,
            nextBuffer.resource.GetAddressOf())) {
        return false;
    }
    if (!CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &uploadHeap, D3D12_HEAP_FLAG_NONE,
            &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nextBuffer.uploadResource.GetAddressOf())) {
        return false;
    }
    nextBuffer.resource->SetName(L"MeshRenderer.StaticInstanceBuffer");
    nextBuffer.uploadResource->SetName(
        L"MeshRenderer.StaticInstanceUploadBuffer");

    void *mapped = nullptr;
    if (!MapResourceChecked(nextBuffer.uploadResource.Get(), &mapped)) {
        return false;
    }
    std::memcpy(mapped, instanceScratch_.data(), bufferSize);
    nextBuffer.uploadResource->Unmap(0, nullptr);

    if (!dxCommon_->BeginUpload()) {
        return false;
    }
    ID3D12GraphicsCommandList *cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        dxCommon_->AbortFrame();
        return false;
    }
    cmd->CopyBufferRegion(nextBuffer.resource.Get(), 0,
                          nextBuffer.uploadResource.Get(), 0, bufferSize);
    auto toVertex = CD3DX12_RESOURCE_BARRIER::Transition(
        nextBuffer.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    cmd->ResourceBarrier(1, &toVertex);
    const DirectXCommon::UploadPassResult uploadResult =
        dxCommon_->EndUploadPass();
    if (uploadResult == DirectXCommon::UploadPassResult::Failed) {
        return false;
    }

    nextBuffer.view.BufferLocation =
        nextBuffer.resource->GetGPUVirtualAddress();
    nextBuffer.view.SizeInBytes = bufferSize;
    nextBuffer.view.StrideInBytes = sizeof(InstanceData);
    nextBuffer.instanceCount = instanceCount;
    nextBuffer.contentHash = contentHash;
    if (uploadResult == DirectXCommon::UploadPassResult::Completed) {
        nextBuffer.uploadResource.Reset();
    }
    buffer = nextBuffer;
    return true;
}

bool MeshRenderer::ReleaseStaticInstanceBuffer(
    MeshInstanceBuffer &buffer, bool allowFrameAbort) noexcept {
    if (!buffer.resource && !buffer.uploadResource) {
        buffer.Reset();
        return true;
    }
    if (dxCommon_ && !dxCommon_->IsDeviceRemoved()) {
        if (dxCommon_->IsCommandListRecording()) {
            if (!allowFrameAbort) {
                return false;
            }
            dxCommon_->AbortFrame();
        }
        if (!dxCommon_->WaitForGpuIfPossible()) {
            return false;
        }
    }
    buffer.Reset();
    return true;
}

bool MeshRenderer::ReleaseGpuCullBuffer(MeshGpuCullBuffer &buffer,
                                        bool allowFrameAbort) noexcept {
    if (!CanReleaseGpuCullResources(dxCommon_, HasGpuCullResources(buffer),
                                    allowFrameAbort)) {
        return false;
    }
    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(&buffer);
    }
    ResetGpuCullDescriptorsAndResources(srvManager_, buffer);
    return true;
}

bool MeshRenderer::ReleaseGpuLodCullBuffer(
    MeshGpuLodCullBuffer &buffer, bool allowFrameAbort) noexcept {
    if (!CanReleaseGpuCullResources(dxCommon_, HasGpuLodCullResources(buffer),
                                    allowFrameAbort)) {
        return false;
    }
    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(&buffer);
    }
    ResetGpuLodCullDescriptorsAndResources(srvManager_, buffer);
    return true;
}

bool MeshRenderer::EnsureGpuCullBuffer(const MeshInstanceBuffer &sourceInstances,
                                       MeshGpuCullBuffer &buffer) {
    if (!dxCommon_ || !dxCommon_->GetDevice() || !srvManager_ ||
        !sourceInstances.IsValid() || !sourceInstances.resource) {
        return false;
    }
    ID3D12Device *device = dxCommon_->GetDevice();
    ID3D12Resource *sourceResource = sourceInstances.resource.Get();
    const uint32_t instanceCount = sourceInstances.instanceCount;
    if (buffer.IsValidFor(instanceCount, sourceResource)) {
        return true;
    }

    const bool hasExistingResources =
        HasGpuCullResources(buffer);
    if (hasExistingResources && dxCommon_->IsCommandListRecording()) {
        return false;
    }
    if (!ReleaseGpuCullBuffer(buffer) || HasGpuCullResources(buffer)) {
        return false;
    }

    if (instanceCount == 0u ||
        instanceCount >
            (std::numeric_limits<UINT>::max)() / sizeof(InstanceData) ||
        !srvManager_->CanAllocateDescriptors(4)) {
        return false;
    }

    const UINT instanceBufferSize =
        static_cast<UINT>(sizeof(InstanceData) * instanceCount);
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto outputDesc = CD3DX12_RESOURCE_DESC::Buffer(
        instanceBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto countDesc = CD3DX12_RESOURCE_DESC::Buffer(
        16u, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto drawArgsDesc = CD3DX12_RESOURCE_DESC::Buffer(
        sizeof(D3D12_DRAW_INDEXED_ARGUMENTS),
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    if (!CreateCommittedResourceChecked(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &outputDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            buffer.outputResource.GetAddressOf()) ||
        !CreateCommittedResourceChecked(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &countDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            buffer.countResource.GetAddressOf()) ||
        !CreateCommittedResourceChecked(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &drawArgsDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            buffer.drawArgsResource.GetAddressOf())) {
        ResetGpuCullDescriptorsAndResources(srvManager_, buffer);
        return false;
    }
    buffer.outputResource->SetName(L"MeshRenderer.GpuCullOutputInstances");
    buffer.countResource->SetName(L"MeshRenderer.GpuCullVisibleCount");
    buffer.drawArgsResource->SetName(L"MeshRenderer.GpuCullDrawArgs");

    if (!AllocateDescriptor(srvManager_, buffer.sourceSrvIndex,
                            buffer.sourceSrvCpuHandle,
                            buffer.sourceSrvGpuHandle) ||
        !AllocateDescriptor(srvManager_, buffer.outputUavIndex,
                            buffer.outputUavCpuHandle,
                            buffer.outputUavGpuHandle) ||
        !AllocateDescriptor(srvManager_, buffer.countUavIndex,
                            buffer.countUavCpuHandle,
                            buffer.countUavGpuHandle) ||
        !AllocateDescriptor(srvManager_, buffer.drawArgsUavIndex,
                            buffer.drawArgsUavCpuHandle,
                            buffer.drawArgsUavGpuHandle)) {
        ResetGpuCullDescriptorsAndResources(srvManager_, buffer);
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC sourceSrvDesc{};
    sourceSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    sourceSrvDesc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sourceSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    sourceSrvDesc.Buffer.FirstElement = 0;
    sourceSrvDesc.Buffer.NumElements = instanceCount;
    sourceSrvDesc.Buffer.StructureByteStride = sizeof(InstanceData);
    sourceSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device->CreateShaderResourceView(sourceResource, &sourceSrvDesc,
                                     buffer.sourceSrvCpuHandle);

    D3D12_UNORDERED_ACCESS_VIEW_DESC outputUavDesc{};
    outputUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    outputUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    outputUavDesc.Buffer.FirstElement = 0;
    outputUavDesc.Buffer.NumElements = instanceCount;
    outputUavDesc.Buffer.StructureByteStride = sizeof(InstanceData);
    outputUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(buffer.outputResource.Get(), nullptr,
                                      &outputUavDesc,
                                      buffer.outputUavCpuHandle);

    D3D12_UNORDERED_ACCESS_VIEW_DESC rawUavDesc{};
    rawUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    rawUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    rawUavDesc.Buffer.FirstElement = 0;
    rawUavDesc.Buffer.NumElements = 4u;
    rawUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device->CreateUnorderedAccessView(buffer.countResource.Get(), nullptr,
                                      &rawUavDesc,
                                      buffer.countUavCpuHandle);

    rawUavDesc.Buffer.NumElements =
        sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) / sizeof(uint32_t);
    device->CreateUnorderedAccessView(buffer.drawArgsResource.Get(), nullptr,
                                      &rawUavDesc,
                                      buffer.drawArgsUavCpuHandle);

    buffer.outputView.BufferLocation =
        buffer.outputResource->GetGPUVirtualAddress();
    buffer.outputView.SizeInBytes = instanceBufferSize;
    buffer.outputView.StrideInBytes = sizeof(InstanceData);
    buffer.maxInstanceCount = instanceCount;
    buffer.sourceResource = sourceResource;
    buffer.sourceInstanceCount = instanceCount;
    buffer.outputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    buffer.drawArgsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    return true;
}

bool MeshRenderer::EnsureGpuLodCullBuffer(
    const MeshInstanceBuffer &sourceInstances, MeshGpuLodCullBuffer &buffer) {
    if (!dxCommon_ || !dxCommon_->GetDevice() || !srvManager_ ||
        !sourceInstances.IsValid() || !sourceInstances.resource) {
        return false;
    }
    ID3D12Device *device = dxCommon_->GetDevice();
    ID3D12Resource *sourceResource = sourceInstances.resource.Get();
    const uint32_t instanceCount = sourceInstances.instanceCount;
    if (buffer.IsValidFor(instanceCount, sourceResource)) {
        return true;
    }

    const bool hasExistingResources = HasGpuLodCullResources(buffer);
    if (hasExistingResources && dxCommon_->IsCommandListRecording()) {
        return false;
    }
    if (!ReleaseGpuLodCullBuffer(buffer) || HasGpuLodCullResources(buffer)) {
        return false;
    }

    if (instanceCount == 0u ||
        instanceCount >
            (std::numeric_limits<UINT>::max)() / sizeof(InstanceData) ||
        !srvManager_->CanAllocateDescriptors(1u + kMeshGpuCullLodCount * 3u)) {
        return false;
    }

    const UINT instanceBufferSize =
        static_cast<UINT>(sizeof(InstanceData) * instanceCount);
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto outputDesc = CD3DX12_RESOURCE_DESC::Buffer(
        instanceBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto countDesc = CD3DX12_RESOURCE_DESC::Buffer(
        16u, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto drawArgsDesc = CD3DX12_RESOURCE_DESC::Buffer(
        sizeof(D3D12_DRAW_INDEXED_ARGUMENTS),
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        if (!CreateCommittedResourceChecked(
                device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &outputDesc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                buffer.outputResources[lod].GetAddressOf()) ||
            !CreateCommittedResourceChecked(
                device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &countDesc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                buffer.countResources[lod].GetAddressOf()) ||
            !CreateCommittedResourceChecked(
                device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &drawArgsDesc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                buffer.drawArgsResources[lod].GetAddressOf())) {
            ResetGpuLodCullDescriptorsAndResources(srvManager_, buffer);
            return false;
        }
        buffer.outputResources[lod]->SetName(
            L"MeshRenderer.GpuLodCullOutputInstances");
        buffer.countResources[lod]->SetName(
            L"MeshRenderer.GpuLodCullVisibleCount");
        buffer.drawArgsResources[lod]->SetName(
            L"MeshRenderer.GpuLodCullDrawArgs");
    }

    if (!AllocateDescriptor(srvManager_, buffer.sourceSrvIndex,
                            buffer.sourceSrvCpuHandle,
                            buffer.sourceSrvGpuHandle)) {
        ResetGpuLodCullDescriptorsAndResources(srvManager_, buffer);
        return false;
    }
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        if (!AllocateDescriptor(srvManager_, buffer.outputUavIndices[lod],
                                buffer.outputUavCpuHandles[lod],
                                buffer.outputUavGpuHandles[lod]) ||
            !AllocateDescriptor(srvManager_, buffer.countUavIndices[lod],
                                buffer.countUavCpuHandles[lod],
                                buffer.countUavGpuHandles[lod]) ||
            !AllocateDescriptor(srvManager_, buffer.drawArgsUavIndices[lod],
                                buffer.drawArgsUavCpuHandles[lod],
                                buffer.drawArgsUavGpuHandles[lod])) {
            ResetGpuLodCullDescriptorsAndResources(srvManager_, buffer);
            return false;
        }
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC sourceSrvDesc{};
    sourceSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    sourceSrvDesc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sourceSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    sourceSrvDesc.Buffer.FirstElement = 0;
    sourceSrvDesc.Buffer.NumElements = instanceCount;
    sourceSrvDesc.Buffer.StructureByteStride = sizeof(InstanceData);
    sourceSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device->CreateShaderResourceView(sourceResource, &sourceSrvDesc,
                                     buffer.sourceSrvCpuHandle);

    D3D12_UNORDERED_ACCESS_VIEW_DESC outputUavDesc{};
    outputUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    outputUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    outputUavDesc.Buffer.FirstElement = 0;
    outputUavDesc.Buffer.NumElements = instanceCount;
    outputUavDesc.Buffer.StructureByteStride = sizeof(InstanceData);
    outputUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    D3D12_UNORDERED_ACCESS_VIEW_DESC rawUavDesc{};
    rawUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    rawUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    rawUavDesc.Buffer.FirstElement = 0;
    rawUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        device->CreateUnorderedAccessView(
            buffer.outputResources[lod].Get(), nullptr, &outputUavDesc,
            buffer.outputUavCpuHandles[lod]);

        rawUavDesc.Buffer.NumElements = 4u;
        device->CreateUnorderedAccessView(
            buffer.countResources[lod].Get(), nullptr, &rawUavDesc,
            buffer.countUavCpuHandles[lod]);

        rawUavDesc.Buffer.NumElements =
            sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) / sizeof(uint32_t);
        device->CreateUnorderedAccessView(
            buffer.drawArgsResources[lod].Get(), nullptr, &rawUavDesc,
            buffer.drawArgsUavCpuHandles[lod]);

        buffer.outputViews[lod].BufferLocation =
            buffer.outputResources[lod]->GetGPUVirtualAddress();
        buffer.outputViews[lod].SizeInBytes = instanceBufferSize;
        buffer.outputViews[lod].StrideInBytes = sizeof(InstanceData);
        buffer.outputStates[lod] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        buffer.drawArgsStates[lod] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    buffer.maxInstanceCount = instanceCount;
    buffer.sourceResource = sourceResource;
    buffer.sourceInstanceCount = instanceCount;
    return true;
}

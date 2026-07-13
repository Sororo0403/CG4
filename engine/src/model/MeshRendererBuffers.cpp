#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "internal/MeshRendererInternal.h"
#include "internal/RendererUploadUtils.h"
#include "model/MeshRenderer.h"
#include "model/RendererMath.h"
#include "model/RendererSceneConstants.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <utility>

using namespace DirectX;

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;
using GpuResourceHelpers::MapResourceChecked;

constexpr size_t kMaxStaticInstanceBufferBytes = 64ull * 1024ull * 1024ull;

bool CanCreateStaticInstanceBuffer(uint32_t instanceCount) {
    if (instanceCount == 0) {
        return false;
    }
    const size_t byteSize = sizeof(InstanceData) * static_cast<size_t>(instanceCount);
    return byteSize <= kMaxStaticInstanceBufferBytes;
}

} // namespace

struct MeshRenderer::StaticInstanceBufferBuild {
    uint32_t instanceCount = 0;
    UINT bufferSize = 0;
    uint64_t contentHash = 0;
    bool ownsUploadPass = false;
    bool releaseUploadResource = false;
    bool unchanged = false;
    MeshInstanceBuffer nextBuffer{};
};

void MeshRenderer::MarkStaticInstanceBufferUsed(const MeshInstanceBuffer& buffer) const {
    if (state_->dxCommon != nullptr && buffer.IsValid()) {
        buffer.lastUsedFrameIndex = state_->dxCommon->GetBackBufferIndex();
    }
}

bool MeshRenderer::RetireStaticInstanceBuffer(MeshInstanceBuffer& buffer) noexcept {
    if (!buffer.resource && !buffer.uploadResource) {
        buffer.Reset();
        return true;
    }
    const UINT frameIndex = buffer.lastUsedFrameIndex;
    if (frameIndex == UINT_MAX) {
        buffer.Reset();
        return true;
    }
    try {
        if (frameIndex < state_->retiredStaticInstanceBuffers.size()) {
            MeshInstanceBuffer retiredBuffer{};
            retiredBuffer.resource = std::move(buffer.resource);
            retiredBuffer.uploadResource = std::move(buffer.uploadResource);
            retiredBuffer.view = std::exchange(buffer.view, {});
            retiredBuffer.instanceCount = std::exchange(buffer.instanceCount, 0u);
            retiredBuffer.contentHash = std::exchange(buffer.contentHash, 0u);
            retiredBuffer.lastUsedFrameIndex = std::exchange(buffer.lastUsedFrameIndex, UINT_MAX);
            state_->retiredStaticInstanceBuffers[frameIndex].push_back(std::move(retiredBuffer));
            return true;
        }
    } catch (...) {
        return false;
    }
    return false;
}

void MeshRenderer::CreateUploadBuffer() {
    RendererUploadUtils::InitializeUploadBuffer(state_->uploadBuffer, state_->dxCommon,
                                                kUploadBytesPerFrame);
}

D3D12_GPU_VIRTUAL_ADDRESS MeshRenderer::WriteObjectConstants(
    const XMMATRIX& wvp, const XMMATRIX& world, const XMMATRIX& worldInverseTranspose) {
    return RendererUploadUtils::WriteObjectConstants(state_->uploadBuffer, wvp, world,
                                                     worldInverseTranspose);
}

D3D12_GPU_VIRTUAL_ADDRESS
MeshRenderer::WriteSceneConstants(const Camera& camera) {
    MeshSceneConstBufferData data{};
    auto* sceneDst = &data;
    sceneDst->cameraPos = {camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z,
                           1.0f};
    sceneDst->keyLightDirection = {state_->currentLighting.keyLightDirection.x,
                                   state_->currentLighting.keyLightDirection.y,
                                   state_->currentLighting.keyLightDirection.z, 0.0f};
    sceneDst->keyLightColor = state_->currentLighting.keyLightColor;
    sceneDst->fillLightDirection = {state_->currentLighting.fillLightDirection.x,
                                    state_->currentLighting.fillLightDirection.y,
                                    state_->currentLighting.fillLightDirection.z, 0.0f};
    sceneDst->fillLightColor = state_->currentLighting.fillLightColor;
    sceneDst->ambientColor = state_->currentLighting.ambientColor;
    for (size_t lightIndex = 0; lightIndex < state_->currentLighting.pointLights.size();
         ++lightIndex) {
        sceneDst->pointLights[lightIndex].positionRange =
            state_->currentLighting.pointLights[lightIndex].positionRange;
        sceneDst->pointLights[lightIndex].colorIntensity =
            state_->currentLighting.pointLights[lightIndex].colorIntensity;
    }
    sceneDst->lightingParams = state_->currentLighting.lightingParams;
    sceneDst->fogColor = state_->currentFog.color;
    sceneDst->fogParams = state_->currentFog.params;
    XMStoreFloat4x4(&sceneDst->viewProjection,
                    XMMatrixTranspose(camera.GetView() * camera.GetProj()));
    XMStoreFloat4x4(&sceneDst->lightViewProjection,
                    XMMatrixTranspose(XMLoadFloat4x4(&state_->shadowLightViewProjection)));
    sceneDst->shadowParams = state_->shadowParams;
    sceneDst->shadowFilterParams = state_->shadowFilterParams;
    sceneDst->customSceneParams0 = state_->customSceneParams0;
    sceneDst->customSceneParams1 = state_->customSceneParams1;
    sceneDst->spotLight.positionRange = state_->currentLighting.spotLight.positionRange;
    sceneDst->spotLight.direction = state_->currentLighting.spotLight.direction;
    sceneDst->spotLight.colorIntensity = state_->currentLighting.spotLight.colorIntensity;
    sceneDst->spotLight.angleParams = state_->currentLighting.spotLight.angleParams;
    XMStoreFloat4x4(&sceneDst->spotLightViewProjection,
                    XMMatrixTranspose(XMLoadFloat4x4(&state_->spotLightViewProjection)));
    sceneDst->spotShadowParams = state_->spotShadowParams;
    sceneDst->spotShadowFilterParams = state_->spotShadowFilterParams;
    const uint64_t hash = RendererUploadUtils::HashBytes(&data, sizeof(data));
    if (state_->sceneConstantsCache.valid && state_->sceneConstantsCache.hash == hash) {
        return state_->sceneConstantsCache.gpu;
    }
    const UploadAllocation allocation = state_->uploadBuffer.Write(data);
    if (allocation.gpu != 0) {
        state_->sceneConstantsCache.hash = hash;
        state_->sceneConstantsCache.gpu = allocation.gpu;
        state_->sceneConstantsCache.valid = true;
    }
    return allocation.gpu;
}

D3D12_GPU_VIRTUAL_ADDRESS
MeshRenderer::WriteShadowSceneConstants(const DirectX::XMFLOAT4X4& lightViewProjection) {
    MeshSceneConstBufferData data{};
    XMStoreFloat4x4(&data.viewProjection, XMMatrixTranspose(XMLoadFloat4x4(&lightViewProjection)));
    data.customSceneParams0 = state_->customSceneParams0;
    data.customSceneParams1 = state_->customSceneParams1;
    const uint64_t hash = RendererUploadUtils::HashBytes(&data, sizeof(data));
    if (state_->shadowSceneConstantsCache.valid && state_->shadowSceneConstantsCache.hash == hash) {
        return state_->shadowSceneConstantsCache.gpu;
    }
    const UploadAllocation allocation = state_->uploadBuffer.Write(data);
    if (allocation.gpu != 0) {
        state_->shadowSceneConstantsCache.hash = hash;
        state_->shadowSceneConstantsCache.gpu = allocation.gpu;
        state_->shadowSceneConstantsCache.valid = true;
    }
    return allocation.gpu;
}

D3D12_GPU_VIRTUAL_ADDRESS
MeshRenderer::WriteMaterialConstants(const Material& material) {
    const uint64_t hash = RendererUploadUtils::HashBytes(&material, sizeof(material));
    if (state_->materialConstantsCache.valid && state_->materialConstantsCache.hash == hash) {
        return state_->materialConstantsCache.gpu;
    }
    const UploadAllocation allocation = state_->uploadBuffer.Write(material);
    if (allocation.gpu != 0) {
        state_->materialConstantsCache.hash = hash;
        state_->materialConstantsCache.gpu = allocation.gpu;
        state_->materialConstantsCache.valid = true;
    }
    return allocation.gpu;
}

D3D12_VERTEX_BUFFER_VIEW
MeshRenderer::WriteInstances(const InstanceData* instances, uint32_t instanceCount) {
    if (!RendererUploadUtils::CanStageInstanceData(state_->uploadBuffer.GetBytesPerFrame(),
                                                   instanceCount)) {
        return {};
    }

    try {
        state_->instanceScratch.resize(instanceCount);
    } catch (const std::exception&) {
        return {};
    }
    for (uint32_t index = 0; index < instanceCount; ++index) {
        state_->instanceScratch[index] = SanitizeInstanceDataForDraw(instances[index]);
    }

    const UploadAllocation allocation = state_->uploadBuffer.WriteArray(
        state_->instanceScratch.data(), state_->instanceScratch.size(), alignof(InstanceData));
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = allocation.gpu;
    view.SizeInBytes = static_cast<UINT>(allocation.size);
    view.StrideInBytes = sizeof(InstanceData);
    return view;
}

bool MeshRenderer::CreateStaticInstanceBuffer(const InstanceData* instances, uint32_t instanceCount,
                                              MeshInstanceBuffer& buffer) {
    if (instanceCount == 0u || instances == nullptr) {
        return ReleaseStaticInstanceBuffer(buffer);
    }
    StaticInstanceBufferBuild build{};
    if (!PrepareStaticInstanceBufferBuild(instances, instanceCount, buffer, build)) {
        return false;
    }
    if (build.unchanged) {
        return true;
    }
    return CreateStaticInstanceResources(build) && UploadStaticInstanceBuffer(build) &&
           CommitStaticInstanceBuffer(buffer, build);
}

bool MeshRenderer::PrepareStaticInstanceBufferBuild(const InstanceData* instances,
                                                    uint32_t instanceCount,
                                                    const MeshInstanceBuffer& currentBuffer,
                                                    StaticInstanceBufferBuild& build) {
    if (!state_->dxCommon || !state_->dxCommon->GetDevice()) {
        return false;
    }
    build.ownsUploadPass = !state_->dxCommon->IsCommandListRecording();
    if (!build.ownsUploadPass && !state_->dxCommon->IsUploadPassActive()) {
        return false;
    }
    if (!CanCreateStaticInstanceBuffer(instanceCount) ||
        instanceCount > (std::numeric_limits<UINT>::max)() / sizeof(InstanceData)) {
        return false;
    }
    try {
        state_->instanceScratch.resize(instanceCount);
    } catch (const std::exception&) {
        return false;
    }
    for (uint32_t index = 0; index < instanceCount; ++index) {
        state_->instanceScratch[index] = SanitizeInstanceDataForDraw(instances[index]);
    }
    const UINT bufferSize =
        static_cast<UINT>(sizeof(InstanceData) * state_->instanceScratch.size());
    const uint64_t contentHash =
        RendererUploadUtils::HashBytes(state_->instanceScratch.data(), bufferSize);
    build.instanceCount = instanceCount;
    build.bufferSize = bufferSize;
    build.contentHash = contentHash;
    build.unchanged = currentBuffer.IsValid() && currentBuffer.instanceCount == instanceCount &&
                      currentBuffer.contentHash == contentHash;
    return true;
}

bool MeshRenderer::CreateStaticInstanceResources(StaticInstanceBufferBuild& build) {
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(build.bufferSize);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(build.bufferSize);

    if (!CreateCommittedResourceChecked(
            state_->dxCommon->GetDevice(), &defaultHeap, D3D12_HEAP_FLAG_NONE, &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, build.nextBuffer.resource.GetAddressOf())) {
        return false;
    }
    if (!CreateCommittedResourceChecked(
            state_->dxCommon->GetDevice(), &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, build.nextBuffer.uploadResource.GetAddressOf())) {
        return false;
    }
    build.nextBuffer.resource->SetName(L"MeshRenderer.StaticInstanceBuffer");
    build.nextBuffer.uploadResource->SetName(L"MeshRenderer.StaticInstanceUploadBuffer");

    void* mapped = nullptr;
    if (!MapResourceChecked(build.nextBuffer.uploadResource.Get(), &mapped)) {
        return false;
    }
    std::memcpy(mapped, state_->instanceScratch.data(), build.bufferSize);
    build.nextBuffer.uploadResource->Unmap(0, nullptr);
    return true;
}

bool MeshRenderer::UploadStaticInstanceBuffer(StaticInstanceBufferBuild& build) {
    if (!state_->dxCommon->BeginUpload()) {
        return false;
    }
    ID3D12GraphicsCommandList* cmd = state_->dxCommon->GetCommandList();
    if (cmd == nullptr) {
        if (build.ownsUploadPass) {
            state_->dxCommon->AbortFrame();
        } else {
            static_cast<void>(state_->dxCommon->EndUploadPass());
        }
        return false;
    }
    cmd->CopyBufferRegion(build.nextBuffer.resource.Get(), 0, build.nextBuffer.uploadResource.Get(),
                          0, build.bufferSize);
    auto toVertex = CD3DX12_RESOURCE_BARRIER::Transition(
        build.nextBuffer.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    cmd->ResourceBarrier(1, &toVertex);
    const DirectXCommon::UploadPassResult uploadResult = state_->dxCommon->EndUploadPass();
    if (uploadResult == DirectXCommon::UploadPassResult::Failed) {
        return false;
    }
    build.releaseUploadResource =
        build.ownsUploadPass && uploadResult == DirectXCommon::UploadPassResult::Completed;
    return true;
}

bool MeshRenderer::CommitStaticInstanceBuffer(MeshInstanceBuffer& buffer,
                                              StaticInstanceBufferBuild& build) {
    build.nextBuffer.view.BufferLocation = build.nextBuffer.resource->GetGPUVirtualAddress();
    build.nextBuffer.view.SizeInBytes = build.bufferSize;
    build.nextBuffer.view.StrideInBytes = sizeof(InstanceData);
    build.nextBuffer.instanceCount = build.instanceCount;
    build.nextBuffer.contentHash = build.contentHash;
    if (build.releaseUploadResource) {
        build.nextBuffer.uploadResource.Reset();
    }
    if ((buffer.resource || buffer.uploadResource) && !RetireStaticInstanceBuffer(buffer)) {
        if (state_->dxCommon && !state_->dxCommon->IsDeviceRemoved()) {
            if (state_->dxCommon->IsCommandListRecording()) {
                return false;
            }
            if (!state_->dxCommon->WaitForGpuIfPossible()) {
                return false;
            }
        }
        buffer.Reset();
    }
    buffer = std::move(build.nextBuffer);
    return true;
}

bool MeshRenderer::ReleaseStaticInstanceBuffer(MeshInstanceBuffer& buffer,
                                               bool allowFrameAbort) noexcept {
    if (!buffer.resource && !buffer.uploadResource) {
        buffer.Reset();
        return true;
    }
    if (RetireStaticInstanceBuffer(buffer)) {
        return true;
    }
    if (state_->dxCommon && !state_->dxCommon->IsDeviceRemoved()) {
        if (state_->dxCommon->IsCommandListRecording()) {
            if (!allowFrameAbort) {
                return false;
            }
            state_->dxCommon->AbortFrame();
        }
        if (!state_->dxCommon->WaitForGpuIfPossible()) {
            return false;
        }
    }
    buffer.Reset();
    return true;
}

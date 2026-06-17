#include "graphics/AdvancedGpuRenderer.h"

#include "ConstantBufferUtils.h"
#include "RootSignatureUtils.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "model/MeshManager.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <pix_win.h>
#include <utility>
#include <vector>
#include <wrl.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {

struct RaytracingPreviewConstants {
    XMFLOAT4X4 inverseViewProjection{};
    XMFLOAT4 cameraPositionTime{};
    XMFLOAT4 outputSize{};
    XMFLOAT4 sunDirectionIntensity{};
    XMFLOAT4 anchorRadius{};
    XMFLOAT4 mirrorOriginIntensity{};
    XMFLOAT4 mirrorRight{};
    XMFLOAT4 mirrorUp{};
    XMFLOAT4 mirrorNormal{};
};

static_assert(sizeof(RaytracingPreviewConstants) % 16 == 0);

UINT AlignTo(UINT value, UINT alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

uint32_t PreviewDimension(uint32_t value, uint32_t minimum,
                          uint32_t maximum) {
    return std::clamp(value, minimum, maximum);
}

UINT64 AlignTo64(UINT64 value, UINT64 alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

constexpr uint32_t kMaxRaytracingSceneInstances = 128u;
constexpr UINT kRayHitGroupCount = 1u;
constexpr UINT kRayDescriptorCount = 4u;
constexpr uint32_t kMirrorRayWidth = 256u;
constexpr uint32_t kMirrorRayHeight = 384u;

struct RayPendingMeshInstance {
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    D3D12_INDEX_BUFFER_VIEW indexView{};
    uint32_t indexCount = 0u;
    XMFLOAT4X4 world{};
    uint32_t instanceId = 0u;
};

struct RayMeshBlasEntry {
    D3D12_GPU_VIRTUAL_ADDRESS vertexAddress = 0u;
    D3D12_GPU_VIRTUAL_ADDRESS indexAddress = 0u;
    uint32_t vertexStride = 0u;
    uint32_t vertexBytes = 0u;
    uint32_t indexCount = 0u;
    ComPtr<ID3D12Resource> blas;
    ComPtr<ID3D12Resource> scratch;
};

void StoreInstanceTransform(D3D12_RAYTRACING_INSTANCE_DESC &desc,
                            const XMFLOAT4X4 &world) {
    desc.Transform[0][0] = world._11;
    desc.Transform[0][1] = world._21;
    desc.Transform[0][2] = world._31;
    desc.Transform[0][3] = world._41;
    desc.Transform[1][0] = world._12;
    desc.Transform[1][1] = world._22;
    desc.Transform[1][2] = world._32;
    desc.Transform[1][3] = world._42;
    desc.Transform[2][0] = world._13;
    desc.Transform[2][1] = world._23;
    desc.Transform[2][2] = world._33;
    desc.Transform[2][3] = world._43;
}

} // namespace

struct AdvancedGpuRenderer::RayConstantFrame {
    ComPtr<ID3D12Resource> resource;
    RaytracingPreviewConstants *mapped = nullptr;

    void Reset() {
        if (resource && mapped != nullptr) {
            resource->Unmap(0, nullptr);
            mapped = nullptr;
        }
        resource.Reset();
    }
};

struct AdvancedGpuRenderer::State {
    ComPtr<ID3D12RootSignature> rayRootSignature;
    ComPtr<ID3D12StateObject> rayStateObject;
    ComPtr<ID3D12StateObjectProperties> rayStateProperties;
    ComPtr<ID3D12Resource> rayShaderTable;
    ComPtr<ID3D12Resource> rayTlas;
    ComPtr<ID3D12Resource> rayTlasScratch;
    ComPtr<ID3D12Resource> rayTlasInstanceBuffer;
    D3D12_RAYTRACING_INSTANCE_DESC *rayTlasMappedInstances = nullptr;
    bool rayAccelerationStructuresReady = false;
    std::vector<RayMeshBlasEntry> rayMeshBlasCache;
    std::vector<RayPendingMeshInstance> raySceneInstances;
    ComPtr<ID3D12Resource> rayOutput;
    ComPtr<ID3D12Resource> rayMirrorOutput;
    D3D12_RESOURCE_STATES rayOutputState =
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES rayMirrorOutputState =
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    std::vector<RayConstantFrame> rayConstantFrames;
    UINT rayDescriptorStart = UINT_MAX;
    uint32_t rayWidth = 128u;
    uint32_t rayHeight = 96u;
    uint32_t mirrorRayWidth = kMirrorRayWidth;
    uint32_t mirrorRayHeight = kMirrorRayHeight;

    ComPtr<ID3D12RootSignature> compositeRootSignature;
    ComPtr<ID3D12PipelineState> compositePipelineState;

    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissorRect{};
    uint32_t width = 1u;
    uint32_t height = 1u;
};

AdvancedGpuRenderer::AdvancedGpuRenderer()
    : state_(std::make_unique<State>()) {}

AdvancedGpuRenderer::~AdvancedGpuRenderer() { Finalize(true); }

void AdvancedGpuRenderer::Initialize(DirectXCommon *dxCommon,
                                     SrvManager *srvManager, uint32_t width,
                                     uint32_t height) {
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager) {
        Finalize();
        return;
    }
    if (!Finalize()) {
        return;
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    Resize(width, height);
    CreateRaytracingResources();
}

bool AdvancedGpuRenderer::Finalize() { return Finalize(false); }

bool AdvancedGpuRenderer::Finalize(bool allowFrameAbort) {
    const bool hasGpuResources =
        state_->rayRootSignature || state_->rayStateObject ||
        state_->rayShaderTable || state_->rayTlas ||
        state_->rayTlasScratch || state_->rayTlasInstanceBuffer ||
        state_->rayOutput || state_->rayMirrorOutput ||
        state_->compositeRootSignature || state_->compositePipelineState ||
        !state_->rayConstantFrames.empty();
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources,
                                allowFrameAbort)) {
        return false;
    }

    for (RayConstantFrame &frame : state_->rayConstantFrames) {
        frame.Reset();
    }
    state_->rayConstantFrames.clear();
    state_->rayStateProperties.Reset();
    state_->rayStateObject.Reset();
    state_->rayShaderTable.Reset();
    if (state_->rayTlasInstanceBuffer &&
        state_->rayTlasMappedInstances != nullptr) {
        state_->rayTlasInstanceBuffer->Unmap(0, nullptr);
        state_->rayTlasMappedInstances = nullptr;
    }
    state_->rayTlasInstanceBuffer.Reset();
    state_->rayTlasScratch.Reset();
    state_->rayTlas.Reset();
    state_->rayMeshBlasCache.clear();
    state_->raySceneInstances.clear();
    state_->rayAccelerationStructuresReady = false;
    state_->rayOutput.Reset();
    state_->rayMirrorOutput.Reset();
    state_->rayRootSignature.Reset();
    state_->compositePipelineState.Reset();
    state_->compositeRootSignature.Reset();
    if (srvManager_ && state_->rayDescriptorStart != UINT_MAX) {
        for (UINT offset = 0; offset < kRayDescriptorCount; ++offset) {
            srvManager_->FreeIfAllocated(state_->rayDescriptorStart + offset);
        }
    }
    state_->rayDescriptorStart = UINT_MAX;
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    return true;
}

void AdvancedGpuRenderer::Resize(uint32_t width, uint32_t height) {
    state_->width = (std::max)(width, 1u);
    state_->height = (std::max)(height, 1u);
    state_->viewport.TopLeftX = 0.0f;
    state_->viewport.TopLeftY = 0.0f;
    state_->viewport.Width = static_cast<float>(state_->width);
    state_->viewport.Height = static_cast<float>(state_->height);
    state_->viewport.MinDepth = 0.0f;
    state_->viewport.MaxDepth = 1.0f;
    state_->scissorRect.left = 0;
    state_->scissorRect.top = 0;
    state_->scissorRect.right = static_cast<LONG>(state_->width);
    state_->scissorRect.bottom = static_cast<LONG>(state_->height);

    const uint32_t nextRayWidth =
        PreviewDimension(state_->width / 4u, 96u, 256u);
    const uint32_t nextRayHeight =
        PreviewDimension(state_->height / 4u, 72u, 192u);
    if (nextRayWidth != state_->rayWidth ||
        nextRayHeight != state_->rayHeight) {
        state_->rayWidth = nextRayWidth;
        state_->rayHeight = nextRayHeight;
        CreateRaytracingOutput();
    }
}

bool AdvancedGpuRenderer::IsRaytracingReady() const {
    return dxCommon_ != nullptr &&
           dxCommon_->GetGpuFeatureCaps().raytracingSupported &&
           state_->rayRootSignature && state_->rayStateObject &&
           state_->rayShaderTable && state_->rayOutput &&
           state_->rayMirrorOutput &&
           state_->rayAccelerationStructuresReady && state_->rayTlas &&
           state_->rayTlasScratch && state_->rayTlasInstanceBuffer &&
           state_->rayTlasMappedInstances != nullptr &&
           state_->compositeRootSignature && state_->compositePipelineState &&
           state_->rayDescriptorStart != UINT_MAX && HasRayConstants();
}

void AdvancedGpuRenderer::ResetRaytracingSceneInstances() {
    state_->raySceneInstances.clear();
}

void AdvancedGpuRenderer::AddRaytracingMeshInstance(
    const Mesh &mesh, const XMFLOAT4X4 &world, uint32_t instanceId) {
    if (!mesh.vertexBuffer || !mesh.indexBuffer || mesh.indexCount == 0u ||
        mesh.vertexStride == 0u || mesh.vbView.BufferLocation == 0u ||
        mesh.ibView.BufferLocation == 0u ||
        mesh.ibView.Format != DXGI_FORMAT_R32_UINT ||
        state_->raySceneInstances.size() >= kMaxRaytracingSceneInstances) {
        return;
    }

    RayPendingMeshInstance instance{};
    instance.vertexBuffer = mesh.vertexBuffer;
    instance.indexBuffer = mesh.indexBuffer;
    instance.vertexView = mesh.vbView;
    instance.indexView = mesh.ibView;
    instance.indexCount = mesh.indexCount;
    instance.world = world;
    instance.instanceId = instanceId;
    state_->raySceneInstances.push_back(std::move(instance));
}

void AdvancedGpuRenderer::CreateRaytracingResources() {
    if (!dxCommon_ || !dxCommon_->GetDevice() ||
        !dxCommon_->GetGpuFeatureCaps().raytracingSupported) {
        return;
    }

    CD3DX12_DESCRIPTOR_RANGE uavRange{};
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    CD3DX12_ROOT_PARAMETER params[3]{};
    params[0].InitAsDescriptorTable(1, &uavRange);
    params[1].InitAsConstantBufferView(0);
    params[2].InitAsShaderResourceView(0);

    CD3DX12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.Init(_countof(params), params, 0, nullptr,
                  D3D12_ROOT_SIGNATURE_FLAG_NONE);
    if (!RootSignatureUtils::CreateRootSignature(
            dxCommon_->GetDevice(), rootDesc, state_->rayRootSignature)) {
        return;
    }

    const UINT frameCount =
        (std::max)(1u, dxCommon_->GetSwapChainBufferCount());
    (void)ConstantBufferUtils::CreateUploadFrames(
        dxCommon_->GetDevice(), frameCount,
        sizeof(RaytracingPreviewConstants), state_->rayConstantFrames,
        &RayConstantFrame::resource, &RayConstantFrame::mapped);

    CreateRaytracingOutput();
    CreateRaytracingStateObject();
    CreateRaytracingShaderTable();
    CreateRaytracingAccelerationStructures();
    CreateRaytracingCompositeResources();
}

void AdvancedGpuRenderer::CreateRaytracingOutput() {
    if (!dxCommon_ || !dxCommon_->GetDevice() || !srvManager_ ||
        !dxCommon_->GetGpuFeatureCaps().raytracingSupported) {
        return;
    }

    if (state_->rayDescriptorStart == UINT_MAX) {
        if (!srvManager_->CanAllocateRange(kRayDescriptorCount)) {
            return;
        }
        state_->rayDescriptorStart =
            srvManager_->AllocateRange(kRayDescriptorCount);
        if (state_->rayDescriptorStart == UINT_MAX) {
            return;
        }
    }

    const auto createOutput =
        [&](uint32_t width, uint32_t height, UINT uavOffset, UINT srvOffset,
            const wchar_t *name, ComPtr<ID3D12Resource> &resource,
            D3D12_RESOURCE_STATES &state) -> bool {
        resource.Reset();
        state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = (std::max)(width, 1u);
        desc.Height = (std::max)(height, 1u);
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
        if (!GpuResourceHelpers::CreateCommittedResourceChecked(
                dxCommon_->GetDevice(), &heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                resource.GetAddressOf())) {
            resource.Reset();
            return false;
        }
        resource->SetName(name);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        dxCommon_->GetDevice()->CreateUnorderedAccessView(
            resource.Get(), nullptr, &uavDesc,
            srvManager_->GetCpuHandle(state_->rayDescriptorStart + uavOffset));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        dxCommon_->GetDevice()->CreateShaderResourceView(
            resource.Get(), &srvDesc,
            srvManager_->GetCpuHandle(state_->rayDescriptorStart + srvOffset));
        return true;
    };

    (void)createOutput(state_->rayWidth, state_->rayHeight, 0u, 1u,
                       L"AdvancedGpuRenderer.RayOutput", state_->rayOutput,
                       state_->rayOutputState);
    (void)createOutput(state_->mirrorRayWidth, state_->mirrorRayHeight, 2u, 3u,
                       L"AdvancedGpuRenderer.RayMirrorOutput",
                       state_->rayMirrorOutput,
                       state_->rayMirrorOutputState);
}

void AdvancedGpuRenderer::CreateRaytracingStateObject() {
    ID3D12Device5 *device = dxCommon_->GetRaytracingDevice();
    if (!device || !state_->rayRootSignature) {
        return;
    }

    auto library =
        ShaderCompiler::Compile(ShaderPaths::AdvancedRaytracingPreview, "",
                                "lib_6_6");
    if (!library) {
        return;
    }

    CD3DX12_STATE_OBJECT_DESC stateObjectDesc(
        D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
    auto dxilLibrary =
        stateObjectDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE shaderBytecode{library->GetBufferPointer(),
                                         library->GetBufferSize()};
    dxilLibrary->SetDXILLibrary(&shaderBytecode);
    dxilLibrary->DefineExport(L"AdvancedGpuRayGen");
    dxilLibrary->DefineExport(L"AdvancedGpuMirrorRayGen");
    dxilLibrary->DefineExport(L"AdvancedGpuMiss");
    dxilLibrary->DefineExport(L"AdvancedGpuClosestHit");

    auto hitGroup =
        stateObjectDesc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitGroup->SetHitGroupExport(L"AdvancedGpuHitGroup");
    hitGroup->SetClosestHitShaderImport(L"AdvancedGpuClosestHit");
    hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    auto shaderConfig =
        stateObjectDesc
            .CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    shaderConfig->Config(16u, 8u);

    auto globalRoot =
        stateObjectDesc
            .CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    globalRoot->SetRootSignature(state_->rayRootSignature.Get());

    auto pipelineConfig =
        stateObjectDesc
            .CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    pipelineConfig->Config(1u);

    if (FAILED(device->CreateStateObject(
            stateObjectDesc, IID_PPV_ARGS(&state_->rayStateObject)))) {
        state_->rayStateObject.Reset();
        return;
    }
    if (FAILED(state_->rayStateObject.As(&state_->rayStateProperties))) {
        state_->rayStateObject.Reset();
        state_->rayStateProperties.Reset();
    }
}

void AdvancedGpuRenderer::CreateRaytracingShaderTable() {
    if (!dxCommon_ || !dxCommon_->GetDevice() || !state_->rayStateProperties) {
        return;
    }

    const void *rayGenIdentifier =
        state_->rayStateProperties->GetShaderIdentifier(L"AdvancedGpuRayGen");
    const void *mirrorRayGenIdentifier =
        state_->rayStateProperties->GetShaderIdentifier(
            L"AdvancedGpuMirrorRayGen");
    const void *missIdentifier =
        state_->rayStateProperties->GetShaderIdentifier(L"AdvancedGpuMiss");
    const void *hitGroupIdentifier =
        state_->rayStateProperties->GetShaderIdentifier(L"AdvancedGpuHitGroup");
    if (rayGenIdentifier == nullptr || mirrorRayGenIdentifier == nullptr ||
        missIdentifier == nullptr ||
        hitGroupIdentifier == nullptr) {
        return;
    }

    const UINT shaderRecordSize =
        AlignTo(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
                D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    const UINT shaderTableSize =
        AlignTo(shaderRecordSize * 4u,
                D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(shaderTableSize);
    if (!GpuResourceHelpers::CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            state_->rayShaderTable.GetAddressOf())) {
        state_->rayShaderTable.Reset();
        return;
    }
    state_->rayShaderTable->SetName(L"AdvancedGpuRenderer.RayShaderTable");

    uint8_t *mapped = nullptr;
    if (!GpuResourceHelpers::MapResourceChecked(state_->rayShaderTable.Get(),
                                                &mapped)) {
        state_->rayShaderTable.Reset();
        return;
    }
    std::memcpy(mapped, rayGenIdentifier,
                D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    std::memcpy(mapped + shaderRecordSize, mirrorRayGenIdentifier,
                D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    std::memcpy(mapped + shaderRecordSize * 2u, missIdentifier,
                D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    std::memcpy(mapped + shaderRecordSize * 3u, hitGroupIdentifier,
                D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    state_->rayShaderTable->Unmap(0, nullptr);
}

void AdvancedGpuRenderer::CreateRaytracingAccelerationStructures() {
    ID3D12Device5 *device = dxCommon_ ? dxCommon_->GetRaytracingDevice()
                                      : nullptr;
    if (!device || !dxCommon_->GetDevice() ||
        !dxCommon_->GetGpuFeatureCaps().raytracingSupported) {
        return;
    }

    state_->rayAccelerationStructuresReady = false;
    state_->rayTlas.Reset();
    state_->rayTlasScratch.Reset();
    if (state_->rayTlasInstanceBuffer &&
        state_->rayTlasMappedInstances != nullptr) {
        state_->rayTlasInstanceBuffer->Unmap(0, nullptr);
    }
    state_->rayTlasMappedInstances = nullptr;
    state_->rayTlasInstanceBuffer.Reset();

    const auto fail = [&]() {
        state_->rayAccelerationStructuresReady = false;
        state_->rayTlas.Reset();
        state_->rayTlasScratch.Reset();
        if (state_->rayTlasInstanceBuffer &&
            state_->rayTlasMappedInstances != nullptr) {
            state_->rayTlasInstanceBuffer->Unmap(0, nullptr);
        }
        state_->rayTlasMappedInstances = nullptr;
        state_->rayTlasInstanceBuffer.Reset();
    };

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInputs{};
    tlasInputs.Type =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlasInputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tlasInputs.NumDescs = kMaxRaytracingSceneInstances;
    tlasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlasInfo{};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&tlasInputs,
                                                           &tlasInfo);
    if (tlasInfo.ResultDataMaxSizeInBytes == 0u ||
        tlasInfo.ScratchDataSizeInBytes == 0u) {
        fail();
        return;
    }

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto tlasDesc = CD3DX12_RESOURCE_DESC::Buffer(
        AlignTo64(tlasInfo.ResultDataMaxSizeInBytes,
                  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!GpuResourceHelpers::CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &defaultHeap, D3D12_HEAP_FLAG_NONE,
            &tlasDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            state_->rayTlas.GetAddressOf())) {
        fail();
        return;
    }
    state_->rayTlas->SetName(L"AdvancedGpuRenderer.TLAS");

    auto tlasScratchDesc = CD3DX12_RESOURCE_DESC::Buffer(
        AlignTo64(tlasInfo.ScratchDataSizeInBytes,
                  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!GpuResourceHelpers::CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &defaultHeap, D3D12_HEAP_FLAG_NONE,
            &tlasScratchDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            state_->rayTlasScratch.GetAddressOf())) {
        fail();
        return;
    }
    state_->rayTlasScratch->SetName(L"AdvancedGpuRenderer.TLASScratch");

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto instanceDesc = CD3DX12_RESOURCE_DESC::Buffer(
        sizeof(D3D12_RAYTRACING_INSTANCE_DESC) *
        kMaxRaytracingSceneInstances);
    if (!GpuResourceHelpers::CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &uploadHeap, D3D12_HEAP_FLAG_NONE,
            &instanceDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
            state_->rayTlasInstanceBuffer.GetAddressOf())) {
        fail();
        return;
    }
    state_->rayTlasInstanceBuffer->SetName(
        L"AdvancedGpuRenderer.TLASInstances");
    if (!GpuResourceHelpers::MapResourceChecked(
            state_->rayTlasInstanceBuffer.Get(),
            &state_->rayTlasMappedInstances)) {
        fail();
        return;
    }

    state_->rayAccelerationStructuresReady = true;
}

void AdvancedGpuRenderer::CreateRaytracingCompositeResources() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }

    CD3DX12_DESCRIPTOR_RANGE textureRange{};
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_ROOT_PARAMETER params[1]{};
    params[0].InitAsDescriptorTable(1, &textureRange,
                                    D3D12_SHADER_VISIBILITY_PIXEL);
    CD3DX12_STATIC_SAMPLER_DESC sampler(0);
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    CD3DX12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.Init(_countof(params), params, 1, &sampler,
                  D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    if (!RootSignatureUtils::CreateRootSignature(
            dxCommon_->GetDevice(), rootDesc,
            state_->compositeRootSignature)) {
        return;
    }

    auto vs =
        ShaderCompiler::Compile(ShaderPaths::PostProcessVS, "main", "vs_6_6");
    auto ps = ShaderCompiler::Compile(
        ShaderPaths::AdvancedRaytracingCompositePS, "main", "ps_6_6");
    if (!vs || !ps) {
        return;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = state_->compositeRootSignature.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
    desc.DSVFormat = DirectXCommon::kDepthStencilFormat;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC depth =
        CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState = depth;

    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.BlendState = blend;

    if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&state_->compositePipelineState)))) {
        state_->compositePipelineState.Reset();
    }
}

bool AdvancedGpuRenderer::HasRayConstants() const {
    if (state_->rayConstantFrames.empty()) {
        return false;
    }
    return std::all_of(state_->rayConstantFrames.begin(),
                       state_->rayConstantFrames.end(),
                       [](const RayConstantFrame &frame) {
                           return frame.resource && frame.mapped != nullptr;
                       });
}

AdvancedGpuRenderer::RayConstantFrame *
AdvancedGpuRenderer::GetCurrentRayConstantFrame() {
    if (state_->rayConstantFrames.empty()) {
        return nullptr;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr
            ? dxCommon_->GetBackBufferIndex() %
                  state_->rayConstantFrames.size()
            : 0u;
    return &state_->rayConstantFrames[frameIndex];
}

void AdvancedGpuRenderer::PopulateCommonRayConstants(
    RayConstantFrame &frame, const XMFLOAT4X4 *viewProjection,
    const XMFLOAT3 &cameraPosition, const XMFLOAT3 &sunDirection,
    const XMFLOAT3 &anchorPosition, float timeSeconds, uint32_t outputWidth,
    uint32_t outputHeight) {
    if (viewProjection != nullptr) {
        XMMATRIX vp = XMLoadFloat4x4(viewProjection);
        XMMATRIX inverseViewProjection = XMMatrixInverse(nullptr, vp);
        XMStoreFloat4x4(&frame.mapped->inverseViewProjection,
                        XMMatrixTranspose(inverseViewProjection));
    } else {
        frame.mapped->inverseViewProjection = {};
    }
    frame.mapped->cameraPositionTime = {cameraPosition.x, cameraPosition.y,
                                        cameraPosition.z, timeSeconds};
    frame.mapped->outputSize = {
        static_cast<float>(outputWidth),
        static_cast<float>(outputHeight),
        1.0f / static_cast<float>((std::max)(outputWidth, 1u)),
        1.0f / static_cast<float>((std::max)(outputHeight, 1u))};

    XMVECTOR sun = XMLoadFloat3(&sunDirection);
    if (XMVectorGetX(XMVector3LengthSq(sun)) <= 0.000001f) {
        sun = XMVectorSet(0.45f, 0.70f, 0.36f, 0.0f);
    }
    XMFLOAT3 normalizedSun{};
    XMStoreFloat3(&normalizedSun, XMVector3Normalize(sun));
    frame.mapped->sunDirectionIntensity = {
        normalizedSun.x, normalizedSun.y, normalizedSun.z, 1.0f};
    frame.mapped->anchorRadius = {anchorPosition.x, anchorPosition.y,
                                  anchorPosition.z, 1.0f};
}

void AdvancedGpuRenderer::TransitionRayOutput(D3D12_RESOURCE_STATES nextState) {
    if (!dxCommon_ || !state_->rayOutput ||
        state_->rayOutputState == nextState) {
        return;
    }
    ID3D12GraphicsCommandList *cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        state_->rayOutput.Get(), state_->rayOutputState, nextState);
    cmd->ResourceBarrier(1, &barrier);
    state_->rayOutputState = nextState;
}

void AdvancedGpuRenderer::TransitionMirrorRayOutput(
    D3D12_RESOURCE_STATES nextState) {
    if (!dxCommon_ || !state_->rayMirrorOutput ||
        state_->rayMirrorOutputState == nextState) {
        return;
    }
    ID3D12GraphicsCommandList *cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        state_->rayMirrorOutput.Get(), state_->rayMirrorOutputState,
        nextState);
    cmd->ResourceBarrier(1, &barrier);
    state_->rayMirrorOutputState = nextState;
}

void AdvancedGpuRenderer::DispatchRaytracingPass(
    ID3D12GraphicsCommandList4 *rayCmd, RayConstantFrame &frame,
    UINT descriptorOffset, UINT rayGenerationRecordIndex, uint32_t width,
    uint32_t height) {
    rayCmd->SetPipelineState1(state_->rayStateObject.Get());
    rayCmd->SetComputeRootSignature(state_->rayRootSignature.Get());
    rayCmd->SetComputeRootDescriptorTable(
        0,
        srvManager_->GetGpuHandle(state_->rayDescriptorStart + descriptorOffset));
    rayCmd->SetComputeRootConstantBufferView(
        1, frame.resource->GetGPUVirtualAddress());
    rayCmd->SetComputeRootShaderResourceView(
        2, state_->rayTlas->GetGPUVirtualAddress());

    D3D12_DISPATCH_RAYS_DESC dispatch{};
    const UINT shaderRecordSize =
        AlignTo(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
                D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    const D3D12_GPU_VIRTUAL_ADDRESS shaderTableStart =
        state_->rayShaderTable->GetGPUVirtualAddress();
    dispatch.RayGenerationShaderRecord.StartAddress =
        shaderTableStart + shaderRecordSize * rayGenerationRecordIndex;
    dispatch.RayGenerationShaderRecord.SizeInBytes = shaderRecordSize;
    dispatch.MissShaderTable.StartAddress =
        shaderTableStart + shaderRecordSize * 2u;
    dispatch.MissShaderTable.SizeInBytes = shaderRecordSize;
    dispatch.MissShaderTable.StrideInBytes = shaderRecordSize;
    dispatch.HitGroupTable.StartAddress =
        shaderTableStart + shaderRecordSize * 3u;
    dispatch.HitGroupTable.SizeInBytes =
        shaderRecordSize * kRayHitGroupCount;
    dispatch.HitGroupTable.StrideInBytes = shaderRecordSize;
    dispatch.Width = width;
    dispatch.Height = height;
    dispatch.Depth = 1u;
    rayCmd->DispatchRays(&dispatch);
}

bool AdvancedGpuRenderer::BuildTopLevelAccelerationStructure() {
    if (!IsRaytracingReady()) {
        return false;
    }
    ID3D12Device5 *device = dxCommon_->GetRaytracingDevice();
    ID3D12GraphicsCommandList4 *cmd = dxCommon_->GetRaytracingCommandList();
    if (device == nullptr || cmd == nullptr) {
        return false;
    }

    const auto ensureMeshBlas =
        [&](const RayPendingMeshInstance &instance)
        -> D3D12_GPU_VIRTUAL_ADDRESS {
        const D3D12_GPU_VIRTUAL_ADDRESS vertexAddress =
            instance.vertexView.BufferLocation;
        const D3D12_GPU_VIRTUAL_ADDRESS indexAddress =
            instance.indexView.BufferLocation;
        const auto cachedBlas = std::find_if(
            state_->rayMeshBlasCache.begin(), state_->rayMeshBlasCache.end(),
            [&](const RayMeshBlasEntry &entry) {
                return entry.vertexAddress == vertexAddress &&
                       entry.indexAddress == indexAddress &&
                       entry.vertexStride == instance.vertexView.StrideInBytes &&
                       entry.vertexBytes == instance.vertexView.SizeInBytes &&
                       entry.indexCount == instance.indexCount && entry.blas;
            });
        if (cachedBlas != state_->rayMeshBlasCache.end()) {
            return cachedBlas->blas->GetGPUVirtualAddress();
        }

        if (!instance.vertexBuffer || !instance.indexBuffer ||
            instance.vertexView.StrideInBytes == 0u ||
            instance.vertexView.SizeInBytes == 0u ||
            instance.indexCount == 0u) {
            return 0u;
        }

        D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
        geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geometry.Triangles.VertexBuffer.StartAddress = vertexAddress;
        geometry.Triangles.VertexBuffer.StrideInBytes =
            instance.vertexView.StrideInBytes;
        geometry.Triangles.VertexCount =
            instance.vertexView.SizeInBytes / instance.vertexView.StrideInBytes;
        geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geometry.Triangles.IndexBuffer = indexAddress;
        geometry.Triangles.IndexCount = instance.indexCount;
        geometry.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.Flags =
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        inputs.NumDescs = 1u;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.pGeometryDescs = &geometry;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
        device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        if (info.ResultDataMaxSizeInBytes == 0u ||
            info.ScratchDataSizeInBytes == 0u) {
            return 0u;
        }

        RayMeshBlasEntry entry{};
        entry.vertexAddress = vertexAddress;
        entry.indexAddress = indexAddress;
        entry.vertexStride = instance.vertexView.StrideInBytes;
        entry.vertexBytes = instance.vertexView.SizeInBytes;
        entry.indexCount = instance.indexCount;

        CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
        auto resultDesc = CD3DX12_RESOURCE_DESC::Buffer(
            AlignTo64(info.ResultDataMaxSizeInBytes,
                      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (!GpuResourceHelpers::CreateCommittedResourceChecked(
                dxCommon_->GetDevice(), &defaultHeap, D3D12_HEAP_FLAG_NONE,
                &resultDesc,
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                entry.blas.GetAddressOf())) {
            return 0u;
        }
        entry.blas->SetName(L"AdvancedGpuRenderer.MeshBLAS");

        auto scratchDesc = CD3DX12_RESOURCE_DESC::Buffer(
            AlignTo64(info.ScratchDataSizeInBytes,
                      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (!GpuResourceHelpers::CreateCommittedResourceChecked(
                dxCommon_->GetDevice(), &defaultHeap, D3D12_HEAP_FLAG_NONE,
                &scratchDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                entry.scratch.GetAddressOf())) {
            return 0u;
        }
        entry.scratch->SetName(L"AdvancedGpuRenderer.MeshBLASScratch");

        D3D12_RESOURCE_BARRIER toRead[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(
                instance.vertexBuffer.Get(),
                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(
                instance.indexBuffer.Get(), D3D12_RESOURCE_STATE_INDEX_BUFFER,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        };
        cmd->ResourceBarrier(_countof(toRead), toRead);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
        buildDesc.Inputs = inputs;
        buildDesc.ScratchAccelerationStructureData =
            entry.scratch->GetGPUVirtualAddress();
        buildDesc.DestAccelerationStructureData =
            entry.blas->GetGPUVirtualAddress();
        cmd->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
        D3D12_RESOURCE_BARRIER blasBarrier =
            CD3DX12_RESOURCE_BARRIER::UAV(entry.blas.Get());
        cmd->ResourceBarrier(1, &blasBarrier);

        D3D12_RESOURCE_BARRIER toDraw[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(
                instance.vertexBuffer.Get(),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
            CD3DX12_RESOURCE_BARRIER::Transition(
                instance.indexBuffer.Get(),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_INDEX_BUFFER),
        };
        cmd->ResourceBarrier(_countof(toDraw), toDraw);

        const D3D12_GPU_VIRTUAL_ADDRESS resultAddress =
            entry.blas->GetGPUVirtualAddress();
        try {
            state_->rayMeshBlasCache.push_back(std::move(entry));
        } catch (...) {
            return 0u;
        }
        return resultAddress;
    };

    uint32_t instanceCount = 0u;
    if (!state_->raySceneInstances.empty()) {
        for (const RayPendingMeshInstance &source :
             state_->raySceneInstances) {
            if (instanceCount >= kMaxRaytracingSceneInstances) {
                break;
            }
            const D3D12_GPU_VIRTUAL_ADDRESS blasAddress =
                ensureMeshBlas(source);
            if (blasAddress == 0u) {
                continue;
            }
            D3D12_RAYTRACING_INSTANCE_DESC &desc =
                state_->rayTlasMappedInstances[instanceCount];
            std::memset(&desc, 0, sizeof(desc));
            StoreInstanceTransform(desc, source.world);
            desc.InstanceID = source.instanceId;
            desc.InstanceContributionToHitGroupIndex = 0u;
            desc.InstanceMask = 0xffu;
            desc.Flags =
                D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
            desc.AccelerationStructure = blasAddress;
            ++instanceCount;
        }
    }

    if (instanceCount == 0u) {
        return false;
    }

    for (uint32_t index = instanceCount;
         index < kMaxRaytracingSceneInstances; ++index) {
        D3D12_RAYTRACING_INSTANCE_DESC &desc =
            state_->rayTlasMappedInstances[index];
        std::memset(&desc, 0, sizeof(desc));
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
    buildDesc.Inputs.Type =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    buildDesc.Inputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    buildDesc.Inputs.NumDescs = instanceCount;
    buildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    buildDesc.Inputs.InstanceDescs =
        state_->rayTlasInstanceBuffer->GetGPUVirtualAddress();
    buildDesc.ScratchAccelerationStructureData =
        state_->rayTlasScratch->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData =
        state_->rayTlas->GetGPUVirtualAddress();
    cmd->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    D3D12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER::UAV(state_->rayTlas.Get());
    cmd->ResourceBarrier(1, &barrier);
    return true;
}

void AdvancedGpuRenderer::DrawRaytracingScene(
    const XMFLOAT4X4 &viewProjection, const XMFLOAT3 &cameraPosition,
    const XMFLOAT3 &sunDirection, const XMFLOAT3 &anchorPosition,
    float timeSeconds) {
    if (!IsRaytracingReady() || !srvManager_) {
        return;
    }

    ID3D12GraphicsCommandList4 *rayCmd =
        dxCommon_->GetRaytracingCommandList();
    ID3D12GraphicsCommandList *cmd = dxCommon_->GetCommandList();
    if (rayCmd == nullptr || cmd == nullptr) {
        return;
    }
    RayConstantFrame *frame = GetCurrentRayConstantFrame();
    if (!frame || !frame->resource || frame->mapped == nullptr) {
        return;
    }

    PopulateCommonRayConstants(*frame, &viewProjection, cameraPosition,
                               sunDirection, anchorPosition, timeSeconds,
                               state_->rayWidth, state_->rayHeight);
    frame->mapped->mirrorOriginIntensity = {0.0f, 0.0f, 0.0f, 0.0f};
    frame->mapped->mirrorRight = {1.0f, 0.0f, 0.0f, 0.0f};
    frame->mapped->mirrorUp = {0.0f, 1.0f, 0.0f, 0.0f};
    frame->mapped->mirrorNormal = {0.0f, 0.0f, -1.0f, 1.0f};

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    PIXBeginEvent(cmd, 0, "AdvancedGpu.DXRPreview");
    if (!BuildTopLevelAccelerationStructure()) {
        PIXEndEvent(cmd);
        return;
    }
    TransitionRayOutput(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchRaytracingPass(rayCmd, *frame, 0u, 0u, state_->rayWidth,
                           state_->rayHeight);

    D3D12_RESOURCE_BARRIER uavBarrier =
        CD3DX12_RESOURCE_BARRIER::UAV(state_->rayOutput.Get());
    cmd->ResourceBarrier(1, &uavBarrier);
    TransitionRayOutput(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    cmd->RSSetViewports(1, &state_->viewport);
    cmd->RSSetScissorRects(1, &state_->scissorRect);
    cmd->SetPipelineState(state_->compositePipelineState.Get());
    cmd->SetGraphicsRootSignature(state_->compositeRootSignature.Get());
    cmd->SetGraphicsRootDescriptorTable(
        0, srvManager_->GetGpuHandle(state_->rayDescriptorStart + 1u));
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
    PIXEndEvent(cmd);
}

bool AdvancedGpuRenderer::DrawRaytracedMirror(
    const MirrorDesc &mirror, const XMFLOAT3 &cameraPosition,
    const XMFLOAT3 &sunDirection, const XMFLOAT3 &anchorPosition,
    float timeSeconds) {
    if (!IsRaytracingReady() || !srvManager_ || !state_->rayMirrorOutput) {
        return false;
    }

    ID3D12GraphicsCommandList4 *rayCmd =
        dxCommon_->GetRaytracingCommandList();
    ID3D12GraphicsCommandList *cmd = dxCommon_->GetCommandList();
    if (rayCmd == nullptr || cmd == nullptr) {
        return false;
    }
    RayConstantFrame *frame = GetCurrentRayConstantFrame();
    if (!frame || !frame->resource || frame->mapped == nullptr) {
        return false;
    }

    const auto normalizeOr =
        [](FXMVECTOR value, FXMVECTOR fallback) -> XMVECTOR {
        return XMVectorGetX(XMVector3LengthSq(value)) > 0.000001f
                   ? XMVector3Normalize(value)
                   : fallback;
    };

    const float mirrorWidth =
        std::isfinite(mirror.width) ? std::clamp(mirror.width, 0.10f, 120.0f)
                                    : 1.0f;
    const float mirrorHeight =
        std::isfinite(mirror.height) ? std::clamp(mirror.height, 0.10f, 120.0f)
                                     : 1.0f;
    const float mirrorIntensity =
        std::isfinite(mirror.intensity)
            ? std::clamp(mirror.intensity, 0.0f, 1.5f)
            : 1.0f;

    XMVECTOR right =
        normalizeOr(XMLoadFloat3(&mirror.right), XMVectorSet(1, 0, 0, 0));
    XMVECTOR up =
        normalizeOr(XMLoadFloat3(&mirror.up), XMVectorSet(0, 1, 0, 0));
    XMVECTOR normal =
        normalizeOr(XMLoadFloat3(&mirror.normal), XMVector3Cross(right, up));
    up = normalizeOr(XMVector3Cross(normal, right),
                     XMVectorSet(0, 1, 0, 0));
    right = normalizeOr(XMVector3Cross(up, normal),
                        XMVectorSet(1, 0, 0, 0));

    const XMVECTOR center = XMLoadFloat3(&mirror.center);
    const XMVECTOR origin = center - right * (mirrorWidth * 0.5f) -
                            up * (mirrorHeight * 0.5f);
    XMFLOAT3 mirrorOrigin{};
    XMFLOAT3 mirrorRight{};
    XMFLOAT3 mirrorUp{};
    XMFLOAT3 mirrorNormal{};
    XMStoreFloat3(&mirrorOrigin, origin);
    XMStoreFloat3(&mirrorRight, right * mirrorWidth);
    XMStoreFloat3(&mirrorUp, up * mirrorHeight);
    XMStoreFloat3(&mirrorNormal, normal);

    PopulateCommonRayConstants(*frame, nullptr, cameraPosition, sunDirection,
                               anchorPosition, timeSeconds,
                               state_->mirrorRayWidth,
                               state_->mirrorRayHeight);
    frame->mapped->mirrorOriginIntensity = {
        mirrorOrigin.x, mirrorOrigin.y, mirrorOrigin.z, mirrorIntensity};
    frame->mapped->mirrorRight = {
        mirrorRight.x, mirrorRight.y, mirrorRight.z, mirrorWidth};
    frame->mapped->mirrorUp = {
        mirrorUp.x, mirrorUp.y, mirrorUp.z, mirrorHeight};
    frame->mapped->mirrorNormal = {
        mirrorNormal.x, mirrorNormal.y, mirrorNormal.z, 1.0f};

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    PIXBeginEvent(cmd, 0, "AdvancedGpu.DXRMirror");
    if (!BuildTopLevelAccelerationStructure()) {
        PIXEndEvent(cmd);
        return false;
    }
    TransitionMirrorRayOutput(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchRaytracingPass(rayCmd, *frame, 2u, 1u, state_->mirrorRayWidth,
                           state_->mirrorRayHeight);

    D3D12_RESOURCE_BARRIER uavBarrier =
        CD3DX12_RESOURCE_BARRIER::UAV(state_->rayMirrorOutput.Get());
    cmd->ResourceBarrier(1, &uavBarrier);
    TransitionMirrorRayOutput(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    PIXEndEvent(cmd);
    return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE
AdvancedGpuRenderer::GetRaytracingMirrorGpuHandle() const {
    if (!IsRaytracingReady() || !srvManager_ ||
        state_->rayDescriptorStart == UINT_MAX) {
        return {};
    }
    return srvManager_->GetGpuHandle(state_->rayDescriptorStart + 3u);
}

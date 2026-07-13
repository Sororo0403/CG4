#include "graphics/VolumetricLightingSystem.h"

#include "camera/Camera.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxUtils.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/RenderTexture.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "internal/ConstantBufferUtils.h"
#include "internal/RootSignatureUtils.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <vector>
#include <wrl.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {

struct VolumetricLightingConstants {
    XMFLOAT4 cameraPositionNearFar{};
    XMFLOAT4 sunDirectionIntensity{};
    XMFLOAT4 sunColorExtinction{};
    XMFLOAT4 volumeParams0{};
    XMFLOAT4 volumeParams1{};
    XMFLOAT4 shadowParams{};
    XMFLOAT4 renderParams{};
    XMFLOAT4X4 inverseViewProjection{};
    XMFLOAT4X4 lightViewProjection{};
};

static_assert(sizeof(VolumetricLightingConstants) % 16 == 0,
              "VolumetricLightingConstants must match HLSL packing");

float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

float AtLeastFinite(float value, float minimum, float fallback) {
    return (std::max)(FiniteOr(value, fallback), minimum);
}

float ClampFinite(float value, float minimum, float maximum, float fallback) {
    return std::clamp(FiniteOr(value, fallback), minimum, maximum);
}

XMFLOAT3 SanitizeDirection(const XMFLOAT3& value) {
    XMVECTOR vector = XMLoadFloat3(&value);
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) ||
        XMVectorGetX(XMVector3LengthSq(vector)) <= 0.000001f) {
        return {0.0f, 1.0f, 0.0f};
    }
    XMFLOAT3 result{};
    XMStoreFloat3(&result, XMVector3Normalize(vector));
    return result;
}

XMFLOAT3 SanitizeColor(const XMFLOAT3& value) {
    return {
        AtLeastFinite(value.x, 0.0f, 1.0f),
        AtLeastFinite(value.y, 0.0f, 0.96f),
        AtLeastFinite(value.z, 0.0f, 0.88f),
    };
}

VolumetricLightingSettings SanitizeSettings(const VolumetricLightingSettings& source) {
    VolumetricLightingSettings settings = source;
    settings.sunDirection = SanitizeDirection(settings.sunDirection);
    settings.sunColor = SanitizeColor(settings.sunColor);
    settings.intensity = AtLeastFinite(settings.intensity, 0.0f, 0.0f);
    settings.extinctionPerMeter = ClampFinite(settings.extinctionPerMeter, 0.0f, 0.08f, 0.00016f);
    settings.scatteringAlbedo = ClampFinite(settings.scatteringAlbedo, 0.0f, 1.0f, 0.92f);
    settings.anisotropy = ClampFinite(settings.anisotropy, 0.0f, 0.94f, 0.76f);
    settings.maxDistanceMeters = ClampFinite(settings.maxDistanceMeters, 0.5f, 1000.0f, 180.0f);
    settings.densityScale = ClampFinite(settings.densityScale, 0.0f, 24.0f, 1.0f);
    settings.heightFogBaseY = ClampFinite(settings.heightFogBaseY, -1000.0f, 1000.0f, -1.0f);
    settings.heightFogFalloffMeters =
        ClampFinite(settings.heightFogFalloffMeters, 0.25f, 300.0f, 12.0f);
    settings.noiseStrength = ClampFinite(settings.noiseStrength, 0.0f, 0.40f, 0.06f);
    settings.timeSeconds = ClampFinite(settings.timeSeconds, -1000000.0f, 1000000.0f, 0.0f);
    settings.sampleCount = std::clamp(settings.sampleCount, 1u, 48u);
    settings.shadow.bias = ClampFinite(settings.shadow.bias, 0.0f, 0.05f, 0.0015f);
    settings.shadow.strength = ClampFinite(settings.shadow.strength, 0.0f, 1.0f, 0.45f);
    settings.shadow.filterRadius = ClampFinite(settings.shadow.filterRadius, 0.0f, 6.0f, 1.45f);
    settings.shadow.depthSoftness =
        ClampFinite(settings.shadow.depthSoftness, 1.0f, 10000.0f, 2600.0f);
    settings.shadow.edgeFade = ClampFinite(settings.shadow.edgeFade, 0.0f, 0.40f, 0.045f);
    settings.enabled = settings.enabled && settings.intensity > 0.0001f &&
                       settings.extinctionPerMeter > 0.0f && settings.densityScale > 0.0f;
    return settings;
}

uint32_t ResolveHalfResolution(int extent) {
    if (extent <= 1) {
        return 1u;
    }
    const uint32_t positiveExtent = static_cast<uint32_t>(extent);
    return positiveExtent / 2u + positiveExtent % 2u;
}

void DrawCompositeTriangle(ID3D12GraphicsCommandList* commandList, ID3D12DescriptorHeap* heap,
                           ID3D12PipelineState* pipelineState, ID3D12RootSignature* rootSignature,
                           const float (&constants)[4], D3D12_GPU_DESCRIPTOR_HANDLE currentHandle,
                           D3D12_GPU_DESCRIPTOR_HANDLE historyHandle) {
    ID3D12DescriptorHeap* heaps[] = {heap};
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetPipelineState(pipelineState);
    commandList->SetGraphicsRootSignature(rootSignature);
    commandList->SetGraphicsRoot32BitConstants(0, 4, constants, 0);
    commandList->SetGraphicsRootDescriptorTable(1, currentHandle);
    commandList->SetGraphicsRootDescriptorTable(2, historyHandle);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
}

bool HasReadyVolumePair(const std::unique_ptr<RenderTexture>& currentVolume,
                        const std::unique_ptr<RenderTexture>& historyVolume) {
    return currentVolume && currentVolume->IsReady() && historyVolume && historyVolume->IsReady();
}

bool TryGetCommandContext(const DirectXCommon* dxCommon, const SrvManager* srvManager,
                          ID3D12GraphicsCommandList*& commandList, ID3D12DescriptorHeap*& heap) {
    commandList = dxCommon != nullptr ? dxCommon->GetCommandList() : nullptr;
    heap = srvManager != nullptr ? srvManager->GetHeap() : nullptr;
    return commandList != nullptr && heap != nullptr;
}

} // namespace

struct VolumetricLightingSystem::ConstantFrame {
    ComPtr<ID3D12Resource> resource;
    VolumetricLightingConstants* mapped = nullptr;

    void Reset() {
        if (resource && mapped != nullptr) {
            resource->Unmap(0, nullptr);
            mapped = nullptr;
        }
        resource.Reset();
    }
};

struct VolumetricLightingSystem::State {
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12RootSignature> compositeRootSignature;
    ComPtr<ID3D12PipelineState> pipelineState;
    ComPtr<ID3D12PipelineState> compositeAddPipelineState;
    ComPtr<ID3D12PipelineState> compositeCopyPipelineState;
    std::vector<ConstantFrame> constantFrames;
    std::unique_ptr<RenderTexture> currentVolume;
    std::unique_ptr<RenderTexture> historyVolume;
    VolumetricLightingSettings settings{};
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissorRect{};
    int width = 1;
    int height = 1;
    bool hasHistory = false;
};

VolumetricLightingSystem::VolumetricLightingSystem() : state_(std::make_unique<State>()) {}

VolumetricLightingSystem::~VolumetricLightingSystem() {
    Finalize(true);
}

void VolumetricLightingSystem::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager,
                                          int width, int height) {
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager) {
        Finalize();
        return;
    }

    if (!Finalize()) {
        return;
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    CreateRootSignature();
    CreateCompositeRootSignature();
    CreatePipelineState();
    CreateConstantBuffers();
    Resize(width, height);
    if (!IsReady()) {
        Finalize();
    }
}

bool VolumetricLightingSystem::Finalize() {
    return Finalize(false);
}

bool VolumetricLightingSystem::Finalize(bool allowFrameAbort) {
    const bool hasGpuResources =
        state_->rootSignature || state_->compositeRootSignature || state_->pipelineState ||
        state_->compositeAddPipelineState || state_->compositeCopyPipelineState ||
        !state_->constantFrames.empty() || state_->currentVolume || state_->historyVolume;
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources, allowFrameAbort)) {
        return false;
    }
    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }
    for (ConstantFrame& frame : state_->constantFrames) {
        frame.Reset();
    }
    if (state_->currentVolume) {
        (void)state_->currentVolume->Release(allowFrameAbort);
    }
    if (state_->historyVolume) {
        (void)state_->historyVolume->Release(allowFrameAbort);
    }
    state_->constantFrames.clear();
    state_->currentVolume.reset();
    state_->historyVolume.reset();
    state_->compositeCopyPipelineState.Reset();
    state_->compositeAddPipelineState.Reset();
    state_->pipelineState.Reset();
    state_->compositeRootSignature.Reset();
    state_->rootSignature.Reset();
    state_->hasHistory = false;
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    return true;
}

bool VolumetricLightingSystem::Resize(int width, int height) {
    const int previousWidth = state_->width;
    const int previousHeight = state_->height;
    const D3D12_VIEWPORT previousViewport = state_->viewport;
    const D3D12_RECT previousScissorRect = state_->scissorRect;
    const bool previousHasHistory = state_->hasHistory;

    state_->width = width > 0 ? width : 1;
    state_->height = height > 0 ? height : 1;
    DxUtils::ConfigureViewportAndScissor(static_cast<UINT>(state_->width),
                                         static_cast<UINT>(state_->height), state_->viewport,
                                         state_->scissorRect);
    state_->hasHistory = false;
    if (!EnsureRenderTextures()) {
        state_->width = previousWidth;
        state_->height = previousHeight;
        state_->viewport = previousViewport;
        state_->scissorRect = previousScissorRect;
        state_->hasHistory = previousHasHistory;
        return false;
    }
    return true;
}

void VolumetricLightingSystem::SetSettings(const VolumetricLightingSettings& settings) {
    state_->settings = SanitizeSettings(settings);
}

const VolumetricLightingSettings& VolumetricLightingSystem::GetSettings() const {
    return state_->settings;
}

bool VolumetricLightingSystem::IsReady() const {
    return dxCommon_ != nullptr && srvManager_ != nullptr && state_->rootSignature &&
           state_->compositeRootSignature && state_->pipelineState &&
           state_->compositeAddPipelineState && state_->compositeCopyPipelineState &&
           HasConstantBuffers();
}

void VolumetricLightingSystem::CreateRootSignature() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }

    CD3DX12_DESCRIPTOR_RANGE depthRange{};
    depthRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE shadowRange{};
    shadowRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

    CD3DX12_ROOT_PARAMETER params[3]{};
    params[0].InitAsDescriptorTable(1, &depthRange, D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsDescriptorTable(1, &shadowRange, D3D12_SHADER_VISIBILITY_PIXEL);
    params[2].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC samplers[2]{};
    samplers[0].Init(0);
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].Init(1);
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, _countof(samplers), samplers,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    RootSignatureUtils::CreateRootSignature(dxCommon_->GetDevice(), desc, state_->rootSignature);
}

void VolumetricLightingSystem::CreateCompositeRootSignature() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }

    CD3DX12_DESCRIPTOR_RANGE currentRange{};
    currentRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE historyRange{};
    historyRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

    CD3DX12_ROOT_PARAMETER params[3]{};
    params[0].InitAsConstants(4, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsDescriptorTable(1, &currentRange, D3D12_SHADER_VISIBILITY_PIXEL);
    params[2].InitAsDescriptorTable(1, &historyRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC sampler{};
    sampler.Init(0);
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    RootSignatureUtils::CreateRootSignature(dxCommon_->GetDevice(), desc,
                                            state_->compositeRootSignature);
}

void VolumetricLightingSystem::CreatePipelineState() {
    auto resetPipelines = [&]() {
        state_->pipelineState.Reset();
        state_->compositeCopyPipelineState.Reset();
        state_->compositeAddPipelineState.Reset();
    };
    resetPipelines();
    if (!dxCommon_ || !dxCommon_->GetDevice() || !state_->rootSignature ||
        !state_->compositeRootSignature) {
        return;
    }

    auto vs = ShaderCompiler::Compile(ShaderPaths::PostProcessVS, "main", "vs_6_6");
    auto ps = ShaderCompiler::Compile(ShaderPaths::VolumetricLightingPS, "main", "ps_6_6");
    auto compositePs =
        ShaderCompiler::Compile(ShaderPaths::VolumetricLightingCompositePS, "main", "ps_6_6");
    if (!vs || !ps || !compositePs) {
        return;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = state_->rootSignature.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    DxUtils::ConfigureFullscreenTrianglePipeline(desc, DirectXCommon::kSceneColorFormat,
                                                 DXGI_FORMAT_UNKNOWN);

    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&state_->pipelineState)))) {
        resetPipelines();
        return;
    }

    desc.pRootSignature = state_->compositeRootSignature.Get();
    desc.PS = {compositePs->GetBufferPointer(), compositePs->GetBufferSize()};
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&state_->compositeCopyPipelineState)))) {
        resetPipelines();
        return;
    }

    D3D12_BLEND_DESC additiveBlend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    additiveBlend.RenderTarget[0].BlendEnable = TRUE;
    additiveBlend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    additiveBlend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    additiveBlend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    additiveBlend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    additiveBlend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    additiveBlend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    additiveBlend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.BlendState = additiveBlend;
    if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&state_->compositeAddPipelineState)))) {
        resetPipelines();
    }
}

void VolumetricLightingSystem::CreateConstantBuffers() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    const UINT frameCount = (std::max)(1u, dxCommon_->GetSwapChainBufferCount());
    (void)ConstantBufferUtils::CreateUploadFrames(
        dxCommon_->GetDevice(), frameCount, sizeof(VolumetricLightingConstants),
        state_->constantFrames, &ConstantFrame::resource, &ConstantFrame::mapped);
}

bool VolumetricLightingSystem::HasConstantBuffers() const {
    if (state_->constantFrames.empty()) {
        return false;
    }
    return std::all_of(
        state_->constantFrames.begin(), state_->constantFrames.end(),
        [](const ConstantFrame& frame) { return frame.resource && frame.mapped != nullptr; });
}

VolumetricLightingSystem::ConstantFrame* VolumetricLightingSystem::GetCurrentConstantFrame() {
    if (state_->constantFrames.empty()) {
        return nullptr;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr ? dxCommon_->GetBackBufferIndex() % state_->constantFrames.size() : 0;
    return &state_->constantFrames[frameIndex];
}

const VolumetricLightingSystem::ConstantFrame* VolumetricLightingSystem::GetCurrentConstantFrame()
    const {
    if (state_->constantFrames.empty()) {
        return nullptr;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr ? dxCommon_->GetBackBufferIndex() % state_->constantFrames.size() : 0;
    return &state_->constantFrames[frameIndex];
}

bool VolumetricLightingSystem::EnsureRenderTextures() {
    if (!dxCommon_ || !srvManager_) {
        return false;
    }

    const int targetWidth = static_cast<int>(ResolveHalfResolution(state_->width));
    const int targetHeight = static_cast<int>(ResolveHalfResolution(state_->height));

    try {
        if (!state_->currentVolume) {
            state_->currentVolume = std::make_unique<RenderTexture>();
        }
        if (!state_->historyVolume) {
            state_->historyVolume = std::make_unique<RenderTexture>();
            state_->hasHistory = false;
        }
    } catch (const std::exception&) {
        return false;
    }

    auto ensureTexture = [&](RenderTexture& texture) {
        if (!texture.IsReady()) {
            texture.Initialize(dxCommon_, srvManager_, targetWidth, targetHeight);
            return texture.IsReady();
        }
        if (texture.GetWidth() != targetWidth || texture.GetHeight() != targetHeight) {
            if (dxCommon_->IsCommandListRecording()) {
                return false;
            }
            if (!texture.Resize(targetWidth, targetHeight)) {
                return false;
            }
            state_->hasHistory = false;
        }
        return texture.IsReady() && texture.GetWidth() == targetWidth &&
               texture.GetHeight() == targetHeight;
    };

    return ensureTexture(*state_->currentVolume) && ensureTexture(*state_->historyVolume);
}

void VolumetricLightingSystem::DrawVolumeTexture(D3D12_GPU_DESCRIPTOR_HANDLE depthHandle,
                                                 D3D12_GPU_DESCRIPTOR_HANDLE shadowHandle,
                                                 D3D12_GPU_VIRTUAL_ADDRESS constantsAddress) {
    if (!state_->currentVolume || !state_->currentVolume->IsReady()) {
        return;
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* heap = srvManager_->GetHeap();
    if (commandList == nullptr || heap == nullptr) {
        return;
    }

    state_->currentVolume->BeginRenderNoDepth({0.0f, 0.0f, 0.0f, 1.0f});
    ID3D12DescriptorHeap* heaps[] = {heap};
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetPipelineState(state_->pipelineState.Get());
    commandList->SetGraphicsRootSignature(state_->rootSignature.Get());
    commandList->SetGraphicsRootDescriptorTable(0, depthHandle);
    commandList->SetGraphicsRootDescriptorTable(1, shadowHandle);
    commandList->SetGraphicsRootConstantBufferView(2, constantsAddress);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
    state_->currentVolume->EndRender();
}

void VolumetricLightingSystem::CompositeToScene() {
    if (!HasReadyVolumePair(state_->currentVolume, state_->historyVolume)) {
        return;
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* heap = srvManager_->GetHeap();
    if (!TryGetCommandContext(dxCommon_, srvManager_, commandList, heap)) {
        return;
    }

    dxCommon_->BeginSceneColorOverlayPass();
    const float compositeConstants[4] = {
        state_->hasHistory ? 0.28f : 0.0f,
        0.22f,
        0.0f,
        0.0f,
    };
    DrawCompositeTriangle(commandList, heap, state_->compositeAddPipelineState.Get(),
                          state_->compositeRootSignature.Get(), compositeConstants,
                          state_->currentVolume->GetGpuHandle(),
                          state_->historyVolume->GetGpuHandle());
    dxCommon_->EndScenePass();
}

void VolumetricLightingSystem::CopyCurrentToHistory() {
    if (!HasReadyVolumePair(state_->currentVolume, state_->historyVolume)) {
        return;
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* heap = srvManager_->GetHeap();
    if (!TryGetCommandContext(dxCommon_, srvManager_, commandList, heap)) {
        return;
    }

    state_->historyVolume->BeginRenderNoDepth({0.0f, 0.0f, 0.0f, 1.0f});
    const float copyConstants[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    DrawCompositeTriangle(commandList, heap, state_->compositeCopyPipelineState.Get(),
                          state_->compositeRootSignature.Get(), copyConstants,
                          state_->currentVolume->GetGpuHandle(),
                          state_->currentVolume->GetGpuHandle());
    state_->historyVolume->EndRender();
    state_->hasHistory = true;
}

void VolumetricLightingSystem::Draw(D3D12_GPU_DESCRIPTOR_HANDLE depthHandle,
                                    D3D12_GPU_DESCRIPTOR_HANDLE shadowHandle, const Camera& camera,
                                    const XMFLOAT4X4& lightViewProjection) {
    if (!IsReady() || !state_->settings.enabled || depthHandle.ptr == 0 || shadowHandle.ptr == 0 ||
        !dxCommon_->GetCommandList()) {
        return;
    }
    if (!EnsureRenderTextures()) {
        return;
    }

    ConstantFrame* constantFrame = GetCurrentConstantFrame();
    if (constantFrame == nullptr || !constantFrame->resource || constantFrame->mapped == nullptr ||
        constantFrame->resource->GetGPUVirtualAddress() == 0) {
        return;
    }

    const VolumetricLightingSettings settings = SanitizeSettings(state_->settings);
    XMMATRIX viewProjection = camera.GetView() * camera.GetProj();
    XMMATRIX inverseViewProjection = XMMatrixInverse(nullptr, viewProjection);
    VolumetricLightingConstants constants{};
    constants.cameraPositionNearFar = {camera.GetPosition().x, camera.GetPosition().y,
                                       camera.GetPosition().z, camera.GetNearZ()};
    constants.sunDirectionIntensity = {settings.sunDirection.x, settings.sunDirection.y,
                                       settings.sunDirection.z, settings.intensity};
    constants.sunColorExtinction = {settings.sunColor.x, settings.sunColor.y, settings.sunColor.z,
                                    settings.extinctionPerMeter};
    constants.volumeParams0 = {settings.scatteringAlbedo, settings.anisotropy,
                               settings.maxDistanceMeters, settings.densityScale};
    constants.volumeParams1 = {settings.heightFogBaseY, settings.heightFogFalloffMeters,
                               settings.noiseStrength, settings.timeSeconds};
    constants.shadowParams = {settings.shadow.bias, settings.shadow.strength,
                              settings.shadow.filterRadius, settings.shadow.depthSoftness};
    constants.renderParams = {1.0f / static_cast<float>((std::max)(state_->width, 1)),
                              1.0f / static_cast<float>((std::max)(state_->height, 1)),
                              static_cast<float>(settings.sampleCount), settings.shadow.edgeFade};
    XMStoreFloat4x4(&constants.inverseViewProjection, XMMatrixTranspose(inverseViewProjection));
    XMStoreFloat4x4(&constants.lightViewProjection,
                    XMMatrixTranspose(XMLoadFloat4x4(&lightViewProjection)));
    *constantFrame->mapped = constants;

    DrawVolumeTexture(depthHandle, shadowHandle, constantFrame->resource->GetGPUVirtualAddress());
    CompositeToScene();
    CopyCurrentToHistory();
}

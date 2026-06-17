#include "graphics/VolumetricLightingSystem.h"

#include "camera/Camera.h"
#include "graphics/DirectXCommon.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "ConstantBufferUtils.h"
#include "RootSignatureUtils.h"

#include <algorithm>
#include <cmath>
#include <pix_win.h>
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

XMFLOAT3 SanitizeDirection(const XMFLOAT3 &value) {
    XMVECTOR vector = XMLoadFloat3(&value);
    if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
        !std::isfinite(value.z) ||
        XMVectorGetX(XMVector3LengthSq(vector)) <= 0.000001f) {
        return {0.0f, 1.0f, 0.0f};
    }
    XMFLOAT3 result{};
    XMStoreFloat3(&result, XMVector3Normalize(vector));
    return result;
}

XMFLOAT3 SanitizeColor(const XMFLOAT3 &value) {
    return {
        AtLeastFinite(value.x, 0.0f, 1.0f),
        AtLeastFinite(value.y, 0.0f, 0.96f),
        AtLeastFinite(value.z, 0.0f, 0.88f),
    };
}

VolumetricLightingSettings SanitizeSettings(
    const VolumetricLightingSettings &source) {
    VolumetricLightingSettings settings = source;
    settings.sunDirection = SanitizeDirection(settings.sunDirection);
    settings.sunColor = SanitizeColor(settings.sunColor);
    settings.intensity = AtLeastFinite(settings.intensity, 0.0f, 0.0f);
    settings.extinctionPerMeter =
        ClampFinite(settings.extinctionPerMeter, 0.0f, 0.08f, 0.00016f);
    settings.scatteringAlbedo =
        ClampFinite(settings.scatteringAlbedo, 0.0f, 1.0f, 0.92f);
    settings.anisotropy =
        ClampFinite(settings.anisotropy, 0.0f, 0.94f, 0.76f);
    settings.maxDistanceMeters =
        ClampFinite(settings.maxDistanceMeters, 0.5f, 1000.0f, 180.0f);
    settings.densityScale =
        ClampFinite(settings.densityScale, 0.0f, 24.0f, 1.0f);
    settings.heightFogBaseY =
        ClampFinite(settings.heightFogBaseY, -1000.0f, 1000.0f, -1.0f);
    settings.heightFogFalloffMeters =
        ClampFinite(settings.heightFogFalloffMeters, 0.25f, 300.0f, 12.0f);
    settings.noiseStrength =
        ClampFinite(settings.noiseStrength, 0.0f, 0.40f, 0.06f);
    settings.timeSeconds =
        ClampFinite(settings.timeSeconds, -1000000.0f, 1000000.0f, 0.0f);
    settings.sampleCount = std::clamp(settings.sampleCount, 1u, 48u);
    settings.shadow.bias =
        ClampFinite(settings.shadow.bias, 0.0f, 0.05f, 0.0015f);
    settings.shadow.strength =
        ClampFinite(settings.shadow.strength, 0.0f, 1.0f, 0.45f);
    settings.shadow.filterRadius =
        ClampFinite(settings.shadow.filterRadius, 0.0f, 6.0f, 1.45f);
    settings.shadow.depthSoftness =
        ClampFinite(settings.shadow.depthSoftness, 1.0f, 10000.0f, 2600.0f);
    settings.shadow.edgeFade =
        ClampFinite(settings.shadow.edgeFade, 0.0f, 0.40f, 0.045f);
    settings.enabled = settings.enabled && settings.intensity > 0.0001f &&
                       settings.extinctionPerMeter > 0.0f &&
                       settings.densityScale > 0.0f;
    return settings;
}

} // namespace

struct VolumetricLightingSystem::ConstantFrame {
    ComPtr<ID3D12Resource> resource;
    VolumetricLightingConstants *mapped = nullptr;

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
    ComPtr<ID3D12PipelineState> pipelineState;
    std::vector<ConstantFrame> constantFrames;
    VolumetricLightingSettings settings{};
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissorRect{};
    int width = 1;
    int height = 1;
};

VolumetricLightingSystem::VolumetricLightingSystem()
    : state_(std::make_unique<State>()) {}

VolumetricLightingSystem::~VolumetricLightingSystem() { Finalize(true); }

void VolumetricLightingSystem::Initialize(DirectXCommon *dxCommon,
                                          SrvManager *srvManager, int width,
                                          int height) {
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
    CreatePipelineState();
    CreateConstantBuffers();
    Resize(width, height);
    if (!IsReady()) {
        Finalize();
    }
}

bool VolumetricLightingSystem::Finalize() { return Finalize(false); }

bool VolumetricLightingSystem::Finalize(bool allowFrameAbort) {
    const bool hasGpuResources = state_->rootSignature || state_->pipelineState ||
                                 !state_->constantFrames.empty();
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources,
                                allowFrameAbort)) {
        return false;
    }
    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }
    for (ConstantFrame &frame : state_->constantFrames) {
        frame.Reset();
    }
    state_->constantFrames.clear();
    state_->pipelineState.Reset();
    state_->rootSignature.Reset();
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    return true;
}

void VolumetricLightingSystem::Resize(int width, int height) {
    state_->width = width > 0 ? width : 1;
    state_->height = height > 0 ? height : 1;
    state_->viewport.TopLeftX = 0.0f;
    state_->viewport.TopLeftY = 0.0f;
    state_->viewport.Width = static_cast<float>(state_->width);
    state_->viewport.Height = static_cast<float>(state_->height);
    state_->viewport.MinDepth = 0.0f;
    state_->viewport.MaxDepth = 1.0f;
    state_->scissorRect.left = 0;
    state_->scissorRect.top = 0;
    state_->scissorRect.right = state_->width;
    state_->scissorRect.bottom = state_->height;
}

void VolumetricLightingSystem::SetSettings(
    const VolumetricLightingSettings &settings) {
    state_->settings = SanitizeSettings(settings);
}

const VolumetricLightingSettings &VolumetricLightingSystem::GetSettings()
    const {
    return state_->settings;
}

bool VolumetricLightingSystem::IsReady() const {
    return dxCommon_ != nullptr && srvManager_ != nullptr &&
           state_->rootSignature && state_->pipelineState &&
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
    params[0].InitAsDescriptorTable(1, &depthRange,
                                    D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsDescriptorTable(1, &shadowRange,
                                    D3D12_SHADER_VISIBILITY_PIXEL);
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

    RootSignatureUtils::CreateRootSignature(dxCommon_->GetDevice(), desc,
                                            state_->rootSignature);
}

void VolumetricLightingSystem::CreatePipelineState() {
    if (!dxCommon_ || !dxCommon_->GetDevice() || !state_->rootSignature) {
        return;
    }

    auto vs =
        ShaderCompiler::Compile(ShaderPaths::PostProcessVS, "main", "vs_6_6");
    auto ps = ShaderCompiler::Compile(ShaderPaths::VolumetricLightingPS,
                                      "main", "ps_6_6");
    if (!vs || !ps) {
        return;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = state_->rootSignature.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
    desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
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
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.BlendState = blend;

    if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&state_->pipelineState)))) {
        state_->pipelineState.Reset();
    }
}

void VolumetricLightingSystem::CreateConstantBuffers() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    const UINT frameCount = (std::max)(1u, dxCommon_->GetSwapChainBufferCount());
    (void)ConstantBufferUtils::CreateUploadFrames(
        dxCommon_->GetDevice(), frameCount, sizeof(VolumetricLightingConstants),
        state_->constantFrames, &ConstantFrame::resource,
        &ConstantFrame::mapped);
}

bool VolumetricLightingSystem::HasConstantBuffers() const {
    if (state_->constantFrames.empty()) {
        return false;
    }
    return std::all_of(state_->constantFrames.begin(),
                       state_->constantFrames.end(),
                       [](const ConstantFrame &frame) {
                           return frame.resource && frame.mapped != nullptr;
                       });
}

VolumetricLightingSystem::ConstantFrame *
VolumetricLightingSystem::GetCurrentConstantFrame() {
    if (state_->constantFrames.empty()) {
        return nullptr;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr
            ? dxCommon_->GetBackBufferIndex() % state_->constantFrames.size()
            : 0;
    return &state_->constantFrames[frameIndex];
}

const VolumetricLightingSystem::ConstantFrame *
VolumetricLightingSystem::GetCurrentConstantFrame() const {
    if (state_->constantFrames.empty()) {
        return nullptr;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr
            ? dxCommon_->GetBackBufferIndex() % state_->constantFrames.size()
            : 0;
    return &state_->constantFrames[frameIndex];
}

void VolumetricLightingSystem::Draw(
    D3D12_GPU_DESCRIPTOR_HANDLE depthHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE shadowHandle, const Camera &camera,
    const XMFLOAT4X4 &lightViewProjection) {
    if (!IsReady() || !state_->settings.enabled || depthHandle.ptr == 0 ||
        shadowHandle.ptr == 0 || !dxCommon_->GetCommandList()) {
        return;
    }

    ConstantFrame *constantFrame = GetCurrentConstantFrame();
    if (constantFrame == nullptr || !constantFrame->resource ||
        constantFrame->mapped == nullptr ||
        constantFrame->resource->GetGPUVirtualAddress() == 0) {
        return;
    }

    const VolumetricLightingSettings settings =
        SanitizeSettings(state_->settings);
    XMMATRIX viewProjection = camera.GetView() * camera.GetProj();
    XMMATRIX inverseViewProjection = XMMatrixInverse(nullptr, viewProjection);
    VolumetricLightingConstants constants{};
    constants.cameraPositionNearFar = {camera.GetPosition().x,
                                       camera.GetPosition().y,
                                       camera.GetPosition().z,
                                       camera.GetNearZ()};
    constants.sunDirectionIntensity = {settings.sunDirection.x,
                                       settings.sunDirection.y,
                                       settings.sunDirection.z,
                                       settings.intensity};
    constants.sunColorExtinction = {settings.sunColor.x, settings.sunColor.y,
                                    settings.sunColor.z,
                                    settings.extinctionPerMeter};
    constants.volumeParams0 = {settings.scatteringAlbedo,
                               settings.anisotropy,
                               settings.maxDistanceMeters,
                               settings.densityScale};
    constants.volumeParams1 = {settings.heightFogBaseY,
                               settings.heightFogFalloffMeters,
                               settings.noiseStrength,
                               settings.timeSeconds};
    constants.shadowParams = {settings.shadow.bias, settings.shadow.strength,
                              settings.shadow.filterRadius,
                              settings.shadow.depthSoftness};
    constants.renderParams = {
        1.0f / static_cast<float>((std::max)(state_->width, 1)),
        1.0f / static_cast<float>((std::max)(state_->height, 1)),
        static_cast<float>(settings.sampleCount), settings.shadow.edgeFade};
    XMStoreFloat4x4(&constants.inverseViewProjection,
                    XMMatrixTranspose(inverseViewProjection));
    XMStoreFloat4x4(&constants.lightViewProjection,
                    XMMatrixTranspose(XMLoadFloat4x4(&lightViewProjection)));
    *constantFrame->mapped = constants;

    ID3D12GraphicsCommandList *commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    if (commandList == nullptr || heap == nullptr) {
        return;
    }

    PIXBeginEvent(commandList, 0, "VolumetricLighting");
    dxCommon_->BeginSceneColorOverlayPass();
    ID3D12DescriptorHeap *heaps[] = {heap};
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->RSSetViewports(1, &state_->viewport);
    commandList->RSSetScissorRects(1, &state_->scissorRect);
    commandList->SetPipelineState(state_->pipelineState.Get());
    commandList->SetGraphicsRootSignature(state_->rootSignature.Get());
    commandList->SetGraphicsRootDescriptorTable(0, depthHandle);
    commandList->SetGraphicsRootDescriptorTable(1, shadowHandle);
    commandList->SetGraphicsRootConstantBufferView(
        2, constantFrame->resource->GetGPUVirtualAddress());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
    dxCommon_->EndScenePass();
    PIXEndEvent(commandList);
}

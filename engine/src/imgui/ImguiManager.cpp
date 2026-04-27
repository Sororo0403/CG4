#ifdef _DEBUG
#include "ImguiManager.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "WinApp.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

static SrvManager *gSrvManager = nullptr;

void ImguiManager::Initialize(WinApp *winApp, DirectXCommon *dxCommon,
                              SrvManager *srvManager) {
    srvManager_ = srvManager;
    gSrvManager = srvManager;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(winApp->GetHwnd());

    ImGui_ImplDX12_InitInfo init_info{};
    init_info.Device = dxCommon->GetDevice();
    init_info.CommandQueue = dxCommon->GetCommandQueue();
    init_info.NumFramesInFlight = dxCommon->GetSwapChainBufferCount();
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init_info.SrvDescriptorHeap = srvManager_->GetHeap();

    init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *,
                                        D3D12_CPU_DESCRIPTOR_HANDLE *out_cpu,
                                        D3D12_GPU_DESCRIPTOR_HANDLE *out_gpu) {
        uint32_t index = gSrvManager->Allocate();
        *out_cpu = gSrvManager->GetCpuHandle(index);
        *out_gpu = gSrvManager->GetGpuHandle(index);
    };

    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *,
                                       D3D12_CPU_DESCRIPTOR_HANDLE,
                                       D3D12_GPU_DESCRIPTOR_HANDLE) {};

    ImGui_ImplDX12_Init(&init_info);
}

void ImguiManager::Begin(ID3D12GraphicsCommandList *commandList) {
    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    commandList->SetDescriptorHeaps(1, heaps);

    ImGui_ImplWin32_NewFrame();
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
}

void ImguiManager::End(ID3D12GraphicsCommandList *commandList) {
    ImGui::Render();

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    commandList->SetDescriptorHeaps(1, heaps);

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}
#endif // _DEBUG

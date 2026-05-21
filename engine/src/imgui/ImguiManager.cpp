#ifdef _DEBUG
#include "imgui/ImguiManager.h"
#include "core/WinApp.h"
#include "graphics/DirectXCommon.h"
#include "graphics/SrvManager.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

void ImguiManager::Initialize(WinApp *winApp, DirectXCommon *dxCommon,
                              SrvManager *srvManager) {
    srvManager_ = srvManager;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(winApp->GetHwnd());

    ImGui_ImplDX12_InitInfo init_info{};
    init_info.Device = dxCommon->GetDevice();
    init_info.CommandQueue = dxCommon->GetCommandQueue();
    init_info.NumFramesInFlight = dxCommon->GetSwapChainBufferCount();
    init_info.RTVFormat = DirectXCommon::kBackBufferFormat;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init_info.UserData = this;
    init_info.SrvDescriptorHeap = srvManager_->GetHeap();

    init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *info,
                                        D3D12_CPU_DESCRIPTOR_HANDLE *out_cpu,
                                        D3D12_GPU_DESCRIPTOR_HANDLE *out_gpu) {
        auto *manager = static_cast<ImguiManager *>(info->UserData);
        uint32_t index = manager->srvManager_->Allocate();
        *out_cpu = manager->srvManager_->GetCpuHandle(index);
        *out_gpu = manager->srvManager_->GetGpuHandle(index);
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
#endif

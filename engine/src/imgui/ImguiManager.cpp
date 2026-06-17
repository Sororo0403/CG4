#ifdef _DEBUG
#include "imgui/ImguiManager.h"
#include "core/ResourceHandle.h"
#include "core/WinApp.h"
#include "graphics/DirectXCommon.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include <cstdint>

namespace {
class ImguiInitializationGuard {
  public:
    explicit ImguiInitializationGuard(ImguiManager &manager)
        : manager_(manager) {}
    ~ImguiInitializationGuard() {
        if (active_) {
            manager_.Finalize();
        }
    }

    ImguiInitializationGuard(const ImguiInitializationGuard &) = delete;
    ImguiInitializationGuard &
    operator=(const ImguiInitializationGuard &) = delete;

    void Commit() { active_ = false; }

  private:
    ImguiManager &manager_;
    bool active_ = true;
};

class ImguiDescriptorAllocationGuard {
  public:
    ImguiDescriptorAllocationGuard(SrvManager &srvManager, uint32_t index)
        : srvManager_(&srvManager), index_(index) {}
    ~ImguiDescriptorAllocationGuard() {
        if (active_ && srvManager_ != nullptr) {
            srvManager_->FreeIfAllocated(index_);
        }
    }

    ImguiDescriptorAllocationGuard(const ImguiDescriptorAllocationGuard &) =
        delete;
    ImguiDescriptorAllocationGuard &
    operator=(const ImguiDescriptorAllocationGuard &) = delete;

    void Commit() { active_ = false; }

  private:
    SrvManager *srvManager_ = nullptr;
    uint32_t index_ = kInvalidResourceId;
    bool active_ = true;
};

void LoadJapaneseImguiFont() {
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->AddFontDefault();
}
} // namespace

ImguiManager::~ImguiManager() {
    Finalize(true);
}

void ImguiManager::Initialize(const WinApp *winApp, DirectXCommon *dxCommon,
                              SrvManager *srvManager) {
    if (!winApp || !dxCommon || !srvManager) {
        Finalize();
        return;
    }

    if (!Finalize()) {
        return;
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    ImguiInitializationGuard initializeGuard(*this);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    contextCreated_ = true;
    ImGui::StyleColorsDark();
    LoadJapaneseImguiFont();

    if (!ImGui_ImplWin32_Init(winApp->GetHwnd())) {
        return;
    }
    win32Initialized_ = true;

    if (!dxCommon->GetDevice() || !dxCommon->GetCommandQueue() ||
        dxCommon->GetSwapChainBufferCount() == 0 || !srvManager_->GetHeap()) {
        return;
    }

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
        if (out_cpu == nullptr || out_gpu == nullptr) {
            return;
        }
        *out_cpu = {};
        *out_gpu = {};

        if (info == nullptr || info->UserData == nullptr) {
            return;
        }
        auto *manager = static_cast<ImguiManager *>(info->UserData);
        if (manager->srvManager_ == nullptr ||
            !manager->srvManager_->CanAllocate()) {
            return;
        }

        uint32_t index = manager->srvManager_->Allocate();
        if (!IsValidResourceId(index)) {
            return;
        }
        ImguiDescriptorAllocationGuard allocationGuard(*manager->srvManager_,
                                                       index);
        const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle =
            manager->srvManager_->GetCpuHandle(index);
        const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle =
            manager->srvManager_->GetGpuHandle(index);
        if (cpuHandle.ptr == 0 || gpuHandle.ptr == 0) {
            return;
        }
        manager->allocatedSrvIndices_.emplace(cpuHandle.ptr, index);
        *out_cpu = cpuHandle;
        *out_gpu = gpuHandle;
        allocationGuard.Commit();
    };

    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *info,
                                       D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                       D3D12_GPU_DESCRIPTOR_HANDLE) {
        if (info == nullptr || info->UserData == nullptr) {
            return;
        }
        auto *manager = static_cast<ImguiManager *>(info->UserData);
        if (manager->srvManager_ == nullptr) {
            return;
        }
        auto it = manager->allocatedSrvIndices_.find(cpuHandle.ptr);
        if (it == manager->allocatedSrvIndices_.end()) {
            return;
        }
        manager->srvManager_->FreeIfAllocated(it->second);
        manager->allocatedSrvIndices_.erase(it);
    };

    if (!ImGui_ImplDX12_Init(&init_info)) {
        return;
    }
    dx12Initialized_ = true;
    initializeGuard.Commit();
}

bool ImguiManager::Finalize() { return Finalize(false); }

bool ImguiManager::Finalize(bool allowFrameAbort) {
    const bool hasGpuResources =
        dx12Initialized_ || !allocatedSrvIndices_.empty();
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources,
                                allowFrameAbort)) {
        return false;
    }

    if (dx12Initialized_) {
        ImGui_ImplDX12_Shutdown();
        dx12Initialized_ = false;
    }
    if (win32Initialized_) {
        ImGui_ImplWin32_Shutdown();
        win32Initialized_ = false;
    }
    if (contextCreated_) {
        ImGui::DestroyContext();
        contextCreated_ = false;
    }

    if (srvManager_ != nullptr) {
        for (const auto &[handlePtr, index] : allocatedSrvIndices_) {
            (void)handlePtr;
            srvManager_->FreeIfAllocated(index);
        }
    }
    allocatedSrvIndices_.clear();
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    return true;
}

bool ImguiManager::IsReady() const {
    return srvManager_ != nullptr && srvManager_->GetHeap() != nullptr &&
           contextCreated_ && win32Initialized_ && dx12Initialized_;
}

void ImguiManager::Begin(ID3D12GraphicsCommandList *commandList) {
    if (!IsReady() || commandList == nullptr) {
        return;
    }

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    commandList->SetDescriptorHeaps(1, heaps);

    ImGui_ImplWin32_NewFrame();
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
}

void ImguiManager::End(ID3D12GraphicsCommandList *commandList) {
    if (!IsReady() || commandList == nullptr) {
        return;
    }

    ImGui::Render();

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    commandList->SetDescriptorHeaps(1, heaps);

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}
#endif

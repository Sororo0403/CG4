#ifdef _DEBUG
#include "imgui/ImguiManager.h"
#include "core/AssetManager.h"
#include "core/ResourceHandle.h"
#include "core/WinApp.h"
#include "graphics/DirectXCommon.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include <cstdint>
#include <filesystem>
#include <string>

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

ImFont *TryLoadImguiFont(const std::filesystem::path &fontPath,
                         const ImWchar *fontRanges) {
    std::error_code existsError;
    if (fontPath.empty() || !std::filesystem::exists(fontPath, existsError) ||
        existsError) {
        return nullptr;
    }

    const std::string fontPathString = fontPath.string();
    if (fontPathString.empty()) {
        return nullptr;
    }

    return ImGui::GetIO().Fonts->AddFontFromFileTTF(
        fontPathString.c_str(), 18.0f, nullptr, fontRanges);
}

void LoadJapaneseImguiFont() {
    ImGuiIO &io = ImGui::GetIO();
    constexpr const char *kExtraJapaneseGlyphs =
        "、。々あいうえおかがきぎくけげこさしじすずせそただちっつ"
        "てでとなにのはぶへべぼまみむめもやょよらりるれわをん"
        "ァアィイウェエォオカキギクグケゲコゴサシジスズセゾタダ"
        "チッツテデトドナネノハバパビピフブプベホポマムメモャヤ"
        "ュョラリルレロワンー一万三上下不世中丹主乱乾了予事亜交"
        "今付代休伸位低体何余作使保倍候値停健側備傷傾像充光入全"
        "八具内円再冠出分切到券刻前剰加効動勾化北十千午半南単厚"
        "反口古台号合向吹告周命品四回囲土在地均垂型基場填境壌声"
        "変夏夕外夜大天太失奥始子存季学完定実家密寒寝寸対専小少"
        "居屋層岐川差布帯帳幅幕平年幹広床底度康延弁弱張強当形影"
        "径待後微心必応急性息感態慣憶成戻所手打抜択押担拡持指採"
        "推描換支敗散数整敷文料斜断新方既日昇明星映昨昼時晴暖暗"
        "更書替最月有朝期木未末本机条東林果枝枯柔柳根桜棄検椰"
        "楽概構標横樹機欠次止正歩残段比気水河泡活流浅海深混済温"
        "湿満準滅滝漏演濡瀬火灯炉炭点焚無然焼熟熱燥牡物状率玉現"
        "球理環生用画畔略疎発白的皮監盤目直相省眠着知短破硬示秋"
        "移程種積空突立端競笛符算節範築簡粒粗糖約紅紙素細終経絵"
        "絶緑線緯縁縦縮繰置習老聞胴胸自致色芒芯花芽苗若茶荷菊華"
        "落葉蔽薄虫蜂蝶行表衰裕西要見視覚親角計記設証詰詳認読調"
        "警象質足距跡路転軸軽輪込近返追退逆透通速遅運過道達違遠"
        "適遮選部配重量金針銀錦鏡長開間降限陽階隙集離雨雪雲震静"
        "非面音響頂類風飛食高鳥鳴黄齢";
    static ImVector<ImWchar> fontRanges;
    fontRanges.clear();
    ImFontGlyphRangesBuilder glyphRangesBuilder;
    glyphRangesBuilder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
    glyphRangesBuilder.AddText(kExtraJapaneseGlyphs);
    glyphRangesBuilder.BuildRanges(&fontRanges);

    ImFont *font = TryLoadImguiFont(
        AssetManager::ResolvePath(
            L"engine/resources/fonts/MPLUS1/MPLUS1-ExtraBold.ttf"),
        fontRanges.Data);
    if (font == nullptr) {
        static constexpr const wchar_t *kFallbackFontPaths[] = {
            L"C:/Windows/Fonts/YuGothB.ttc",
            L"C:/Windows/Fonts/YuGothM.ttc",
            L"C:/Windows/Fonts/meiryob.ttc",
            L"C:/Windows/Fonts/meiryo.ttc",
        };
        for (const wchar_t *fallbackPath : kFallbackFontPaths) {
            font = TryLoadImguiFont(fallbackPath, fontRanges.Data);
            if (font != nullptr) {
                break;
            }
        }
    }
    if (font != nullptr) {
        io.FontDefault = font;
    } else {
        io.Fonts->AddFontDefault();
    }
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

#pragma once
#ifdef _DEBUG
#include <d3d12.h>
#include <unordered_map>

class DirectXCommon;
struct ImGui_ImplDX12_InitInfo;
class SrvManager;
class WinApp;

/// <summary>
/// ImGui の初期化とフレーム処理を管理する
/// </summary>
class ImguiManager {
public:
    ~ImguiManager();

    /// <summary>
    /// ImGuiのWin32/DX12バックエンドを初期化する
    /// </summary>
    /// <param name="winApp">WinAppインスタンス</param>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="srvManager">SrvManagerインスタンス</param>
    void Initialize(const WinApp* winApp, DirectXCommon* dxCommon, SrvManager* srvManager);

    /// <summary>
    /// ImGuiバックエンドとSRV割り当てを解放する
    /// </summary>
    bool Finalize();
    bool Finalize(bool allowFrameAbort);

    /// <summary>
    /// ImGuiの新しいフレームを開始する
    /// </summary>
    /// <param name="commandList">コマンドリスト</param>
    void Begin(ID3D12GraphicsCommandList* commandList);

    /// <summary>
    /// ImGuiの描画データをコマンドリストへ積む
    /// </summary>
    /// <param name="commandList">コマンドリスト</param>
    void End(ID3D12GraphicsCommandList* commandList);
    bool IsReady() const;

private:
    bool TryInitializeImguiContext(const WinApp* winApp);
    bool HasDx12BackendRequirements(const DirectXCommon* dxCommon) const;
    ImGui_ImplDX12_InitInfo CreateDx12InitInfo(const DirectXCommon* dxCommon);
    static void AllocateDx12Srv(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                                D3D12_GPU_DESCRIPTOR_HANDLE* outGpu);
    static void FreeDx12Srv(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    std::unordered_map<SIZE_T, UINT> allocatedSrvIndices_;
    bool contextCreated_ = false;
    bool win32Initialized_ = false;
    bool dx12Initialized_ = false;
};

#endif

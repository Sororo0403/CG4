#pragma once
#ifdef _DEBUG
#include <d3d12.h>

class DirectXCommon;
class SrvManager;
class WinApp;

/// <summary>
/// ImGui の初期化とフレーム処理を管理する
/// </summary>
class ImguiManager {
  public:
    /// <summary>
    /// ImGuiのWin32/DX12バックエンドを初期化する
    /// </summary>
    /// <param name="winApp">WinAppインスタンス</param>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="srvManager">SrvManagerインスタンス</param>
    void Initialize(WinApp *winApp, DirectXCommon *dxCommon,
                    SrvManager *srvManager);

    /// <summary>
    /// ImGuiの新しいフレームを開始する
    /// </summary>
    /// <param name="commandList">コマンドリスト</param>
    void Begin(ID3D12GraphicsCommandList *commandList);

    /// <summary>
    /// ImGuiの描画データをコマンドリストへ積む
    /// </summary>
    /// <param name="commandList">コマンドリスト</param>
    void End(ID3D12GraphicsCommandList *commandList);

  private:
    SrvManager *srvManager_ = nullptr;
};

#endif

#pragma once
#ifdef _DEBUG
#include <d3d12.h>

class DirectXCommon;
class SrvManager;
class WinApp;

class ImguiManager {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="winApp">WinAppインスタンス</param>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="srvManager">SrvManagerインスタンス</param>
    void Initialize(WinApp *winApp, DirectXCommon *dxCommon,
                    SrvManager *srvManager);

    /// <summary>
    /// フレーム前処理
    /// </summary>
    /// <param name="commandList">コマンドリスト</param>
    void Begin(ID3D12GraphicsCommandList *commandList);

    /// <summary>
    /// フレーム後処理
    /// </summary>
    /// <param name="commandList">コマンドリスト</param>
    void End(ID3D12GraphicsCommandList *commandList);

  private:
    SrvManager *srvManager_ = nullptr;
};

#endif // _DEBUG
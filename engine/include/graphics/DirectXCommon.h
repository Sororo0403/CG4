#pragma once
#include <DirectXMath.h>
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <functional>
#include <memory>
#include <string>

class DirectXCommonGpuDiagnostics;
class SrvManager;

/// <summary>
/// Direct3D 12 のデバイスと描画フレーム管理を担う
/// </summary>
class DirectXCommon {
public:
    enum class UploadPassResult {
        Failed,
        Submitted,
        Completed,
    };

    static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_FORMAT kSceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    static constexpr DXGI_FORMAT kDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    static constexpr DXGI_FORMAT kDepthResourceFormat = DXGI_FORMAT_R24G8_TYPELESS;
    static constexpr DXGI_FORMAT kDepthSrvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

    DirectXCommon();
    ~DirectXCommon();

    DirectXCommon(const DirectXCommon&) = delete;
    DirectXCommon& operator=(const DirectXCommon&) = delete;
    DirectXCommon(DirectXCommon&&) = delete;
    DirectXCommon& operator=(DirectXCommon&&) = delete;

    /// <summary>
    /// Direct3D 12のデバイス、スワップチェーン、描画先を初期化する
    /// </summary>
    /// <param name="hwnd">ウィンドウハンドル</param>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    /// <summary>
    /// 必要なリソースを初期化する
    /// </summary>
    bool Initialize(HWND hwnd, int width, int height);

    /// <summary>
    /// フレーム描画用コマンドリストの記録を開始する
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// フレーム描画コマンドを実行して画面へ表示する
    /// </summary>
    bool EndFrame();

    /// <summary>
    /// 例外などでフレームを完了できない場合に記録状態を破棄する
    /// </summary>
    void AbortFrame() noexcept;

    bool ReserveFrameRollbacks(size_t additional);
    bool RegisterFrameRollback(std::function<void()> rollback);
    bool RegisterFrameRollback(const void* owner, std::function<void()> rollback);
    void UnregisterFrameRollbacks(const void* owner) noexcept;

    /// <summary>
    /// シーンカラー用レンダーターゲットへの描画状態に切り替える
    /// </summary>
    void BeginScenePass();

    /// <summary>
    /// シーンカラーRTへ描画できる状態を復元する
    /// </summary>
    /// <param name="clearDepth">
    /// trueの場合、意図的に深度を破棄する。透明エフェクト前の復元ではfalseにする。
    /// </param>
    /// <summary>
    /// RestoreSceneRenderStateを実行する
    /// </summary>
    void RestoreSceneRenderState(bool clearDepth = false);

    /// <summary>
    /// 深度をSRVとして読んだまま、シーンカラーRTへ加算描画できる状態へ戻す
    /// </summary>
    void BeginSceneColorOverlayPass();

    /// <summary>
    /// 現在の描画先を維持したまま深度だけをクリアする
    /// </summary>
    void ClearDepth();

    /// <summary>
    /// シーンカラー用レンダーターゲットをシェーダー読み取り可能な状態へ戻す
    /// </summary>
    void EndScenePass();

    /// <summary>
    /// バックバッファへ描画するためのリソース状態に切り替える
    /// </summary>
    void BeginBackBufferPass(bool bindDepth = true);

    /// <summary>
    /// 現在のバックバッファを描画先に設定する
    /// </summary>
    /// <param name="clear">trueの場合、色と深度をクリアする</param>
    void SetBackBufferRenderTarget(bool clear = false, bool bindDepth = true);

    /// <summary>
    /// 深度バッファをシェーダーから読めるSRVとして登録する
    /// </summary>
    bool CreateDepthStencilSrv(SrvManager* srvManager);

    /// <summary>
    /// シーンカラーをシェーダーから読めるSRVとして登録する
    /// </summary>
    bool RegisterSceneColorSRV(SrvManager* srvManager);

    /// <summary>
    /// DirectXCommonがSrvManagerから確保したSRVを解放する
    /// </summary>
    void ReleaseRegisteredSrvs();

    /// <summary>
    /// フレーム開始時のクリア色を設定する
    /// </summary>
    void SetClearColor(const DirectX::XMFLOAT4& color);

    /// <summary>
    /// フレーム開始時のクリア色を設定する
    /// </summary>
    void SetClearColor(float r, float g, float b, float a);

    /// <summary>
    /// フレーム開始時のクリア色を初期値に戻す
    /// </summary>
    void ResetClearColor();

    /// <summary>
    /// 深度バッファをシェーダー読み取り状態へ遷移する
    /// </summary>
    void TransitionDepthToShaderResource();

    /// <summary>
    /// 深度バッファを深度書き込み状態へ遷移する
    /// </summary>
    void TransitionDepthToWrite();

    /// <summary>
    /// 描画ターゲットと深度バッファを新しいサイズに合わせて再生成する
    /// </summary>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    bool Resize(int width, int height);

    /// <summary>
    /// リソースアップロード用コマンドリストの記録を開始する
    /// </summary>
    bool BeginUpload();

    /// <summary>
    /// リソースアップロード用コマンドを実行して完了を待つ
    /// </summary>
    UploadPassResult EndUploadPass();
    bool EndUpload();

    /// <summary>
    /// コマンドキューへFenceを送信し、GPU処理の完了を待機する
    /// </summary>
    bool WaitForGpu();
    bool WaitForGpuIfPossible();

    /// <summary>
    /// GPU同期に必要なD3D12オブジェクトが初期化済みかを取得する
    /// </summary>
    bool IsInitialized() const;

    bool IsReadyForRendering() const;

    /// <summary>
    /// D3D12デバイスを取得する
    /// </summary>
    /// <returns>D3D12デバイス</returns>
    ID3D12Device* GetDevice() const;

    /// <summary>
    /// コマンドキューを取得する
    /// </summary>
    /// <returns>コマンドキュー</returns>
    ID3D12CommandQueue* GetCommandQueue() const;

    /// <summary>
    /// 記録中のグラフィックスコマンドリストを取得する
    /// </summary>
    /// <returns>記録中でない場合はnullptr</returns>
    ID3D12GraphicsCommandList* GetCommandList() const;

    /// <summary>
    /// コマンドリストが記録中かを取得する
    /// </summary>
    bool IsCommandListRecording() const;
    bool IsUploadPassActive() const;

    /// <summary>
    /// スワップチェーンのバッファ数を取得する
    /// </summary>
    /// <returns>バックバッファ数</returns>
    UINT GetSwapChainBufferCount() const {
        return kSwapChainBufferCount;
    }

    /// <summary>
    /// 現在記録中のバックバッファインデックスを取得する
    /// </summary>
    UINT GetBackBufferIndex() const;

    uint32_t GetSceneWidth() const;
    uint32_t GetSceneHeight() const;

    /// <summary>
    /// 深度ステンシルビューのCPUハンドルを取得する
    /// </summary>
    D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const;

    /// <summary>
    /// 深度SRVのGPUハンドルを取得する
    /// </summary>
    D3D12_GPU_DESCRIPTOR_HANDLE GetDepthStencilGpuHandle() const;

    /// <summary>
    /// シーンカラー用リソースを取得する
    /// </summary>
    ID3D12Resource* GetSceneColorBuffer() const;

    /// <summary>
    /// シーンカラーSRVのインデックスを取得する
    /// </summary>
    UINT GetSceneSrvIndex() const;

    /// <summary>
    /// シーンカラーSRVのGPUハンドルを取得する
    /// </summary>
    D3D12_GPU_DESCRIPTOR_HANDLE
    GetSceneSrvGpuHandle(const SrvManager* srvManager) const;

    /// <summary>
    /// 現在のD3D12デバイスがすでに取り外し状態かを取得する
    /// </summary>
    bool IsDeviceRemoved() const;

    /// <summary>
    /// 選択されたGPUアダプタのベンダIDを取得する
    /// </summary>
    UINT GetAdapterVendorId() const;

    /// <summary>
    /// Intel GPU上で動作しているかを取得する
    /// </summary>
    bool IsIntelAdapter() const;

    /// <summary>
    /// 選択されたGPUアダプタ名を取得する
    /// </summary>
    std::wstring GetAdapterDescription() const;

private:
    /// <summary>
    /// DXGIファクトリを生成する
    /// </summary>
    void CreateFactory();

    /// <summary>
    /// D3D12デバイスを生成する
    /// </summary>
    void CreateDevice();

    /// <summary>
    /// コマンドキューを生成する
    /// </summary>
    void CreateCommandQueue();

    /// <summary>
    /// コマンドアロケータを生成する
    /// </summary>
    void CreateCommandAllocator();

    /// <summary>
    /// コマンドリストを生成する
    /// </summary>
    void CreateCommandList();

    /// <summary>
    /// スワップチェーンを生成する
    /// </summary>
    void CreateSwapChain(HWND hwnd, int width, int height);

    /// <summary>
    /// RTVヒープを生成する
    /// </summary>
    void CreateRTV();

    /// <summary>
    /// シーンカラー用RTを生成する
    /// </summary>
    void CreateSceneRenderTarget(int width, int height);

    /// <summary>
    /// ビューポートを設定する
    /// </summary>
    void CreateViewport(int width, int height);

    /// <summary>
    /// シザー矩形を設定する
    /// </summary>
    void CreateScissor(int width, int height);

    /// <summary>
    /// 深度ステンシルバッファを生成する
    /// </summary>
    void CreateDepthStencil(int width, int height);

    /// <summary>
    /// 深度ステンシルSRVの参照先を更新する
    /// </summary>
    bool UpdateDepthStencilSrv();

    /// <summary>
    /// シーンカラーSRVの参照先を更新する
    /// </summary>
    bool UpdateSceneColorSrv();

    /// <summary>
    /// シーン描画用のビューポートとシザー矩形を適用する
    /// </summary>
    void ApplySceneViewportAndScissor();

    /// <summary>
    /// シーンカラーRTと深度ステンシルを描画先に設定する
    /// </summary>
    void BindSceneRenderTarget(bool clearColor, bool clearDepth);

    /// <summary>
    /// シーンカラーRTのリソース状態を必要な状態へ遷移する
    /// </summary>
    /// <remarks>
    /// sceneColorBuffer_のResourceBarrierは必ずこの関数を通す。
    /// </remarks>
    /// <summary>
    /// TransitionSceneColorを実行する
    /// </summary>
    void TransitionSceneColor(D3D12_RESOURCE_STATES afterState);

    /// <summary>
    /// バックバッファのリソース状態を必要な状態へ遷移する
    /// </summary>
    void TransitionBackBuffer(UINT index, D3D12_RESOURCE_STATES afterState);

    /// <summary>
    /// 現在のバックバッファRTVハンドルを取得する
    /// </summary>
    D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferRtvHandle() const;

    /// <summary>
    /// シーンカラーRTVハンドルを取得する
    /// </summary>
    D3D12_CPU_DESCRIPTOR_HANDLE GetSceneRtvHandle() const;

    /// <summary>
    /// GPU同期用フェンスを生成する
    /// </summary>
    void CreateFence();

    /// <summary>
    /// 指定フレームのコマンドアロケータをGPUが使い終わるまで待つ
    /// </summary>
    void WaitForFrame(UINT frameIndex);

    void TrackGpuPhase(const char* phase);
    bool HasFrameResources() const;
    void SnapshotFrameResourceStates();
    void RestoreFrameResourceStates() noexcept;
    void ClearFrameResourceStateSnapshot() noexcept;
    void RestoreFrameRollbacks() noexcept;
    void ClearFrameRollbacks() noexcept;

private:
    static constexpr UINT kSwapChainBufferCount = 2;
    static constexpr UINT kSceneRtvIndex = kSwapChainBufferCount;
    static constexpr float kClearColor[4] = {0.030f, 0.026f, 0.055f, 1.0f};

    struct State;
    std::unique_ptr<State> state_;
};

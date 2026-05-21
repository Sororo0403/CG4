#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

class SrvManager;

/// <summary>
/// Direct3D 12 のデバイスと描画フレーム管理を担う
/// </summary>
class DirectXCommon {
  public:
    static constexpr DXGI_FORMAT kBackBufferFormat =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_FORMAT kSceneColorFormat =
        DXGI_FORMAT_R16G16B16A16_FLOAT;
    static constexpr DXGI_FORMAT kDepthStencilFormat =
        DXGI_FORMAT_D24_UNORM_S8_UINT;
    static constexpr DXGI_FORMAT kDepthResourceFormat =
        DXGI_FORMAT_R24G8_TYPELESS;
    static constexpr DXGI_FORMAT kDepthSrvFormat =
        DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

    DirectXCommon() = default;
    ~DirectXCommon();

    DirectXCommon(const DirectXCommon &) = delete;
    DirectXCommon &operator=(const DirectXCommon &) = delete;
    DirectXCommon(DirectXCommon &&) = delete;
    DirectXCommon &operator=(DirectXCommon &&) = delete;

    /// <summary>
    /// Direct3D 12のデバイス、スワップチェーン、描画先を初期化する
    /// </summary>
    /// <param name="hwnd">ウィンドウハンドル</param>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    void Initialize(HWND hwnd, int width, int height);

    /// <summary>
    /// フレーム描画用コマンドリストの記録を開始する
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// フレーム描画コマンドを実行して画面へ表示する
    /// </summary>
    void EndFrame();

    /// <summary>
    /// シーンカラー用レンダーターゲットへの描画状態に切り替える
    /// </summary>
    void BeginScenePass();

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
    void CreateDepthStencilSrv(SrvManager *srvManager);

    /// <summary>
    /// シーンカラーをシェーダーから読めるSRVとして登録する
    /// </summary>
    void RegisterSceneColorSRV(SrvManager *srvManager);

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
    void Resize(int width, int height);

    /// <summary>
    /// リソースアップロード用コマンドリストの記録を開始する
    /// </summary>
    void BeginUpload();

    /// <summary>
    /// リソースアップロード用コマンドを実行して完了を待つ
    /// </summary>
    void EndUpload();

    /// <summary>
    /// コマンドキューへFenceを送信し、GPU処理の完了を待機する
    /// </summary>
    void WaitForGpu();

    /// <summary>
    /// D3D12デバイスを取得する
    /// </summary>
    /// <returns>D3D12デバイス</returns>
    ID3D12Device *GetDevice() const { return device_.Get(); }

    /// <summary>
    /// コマンドキューを取得する
    /// </summary>
    /// <returns>コマンドキュー</returns>
    ID3D12CommandQueue *GetCommandQueue() const { return commandQueue_.Get(); }

    /// <summary>
    /// グラフィックスコマンドリストを取得する
    /// </summary>
    /// <returns>コマンドリスト</returns>
    ID3D12GraphicsCommandList *GetCommandList() const {
        return commandList_.Get();
    }

    /// <summary>
    /// コマンドリストが記録中かを取得する
    /// </summary>
    bool IsCommandListRecording() const { return isCommandListRecording_; }

    /// <summary>
    /// スワップチェーンのバッファ数を取得する
    /// </summary>
    /// <returns>バックバッファ数</returns>
    UINT GetSwapChainBufferCount() const { return kSwapChainBufferCount; }

    /// <summary>
    /// 現在記録中のバックバッファインデックスを取得する
    /// </summary>
    UINT GetBackBufferIndex() const { return backBufferIndex_; }

    /// <summary>
    /// 深度ステンシルビューのCPUハンドルを取得する
    /// </summary>
    D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const {
        return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    }

    /// <summary>
    /// 深度SRVのGPUハンドルを取得する
    /// </summary>
    D3D12_GPU_DESCRIPTOR_HANDLE GetDepthStencilGpuHandle() const {
        return depthSrvGpuHandle_;
    }

    /// <summary>
    /// シーンカラー用リソースを取得する
    /// </summary>
    ID3D12Resource *GetSceneColorBuffer() const {
        return sceneColorBuffer_.Get();
    }

    /// <summary>
    /// シーンカラーSRVのインデックスを取得する
    /// </summary>
    UINT GetSceneSrvIndex() const { return sceneSrvIndex_; }

    /// <summary>
    /// シーンカラーSRVのGPUハンドルを取得する
    /// </summary>
    D3D12_GPU_DESCRIPTOR_HANDLE
    GetSceneSrvGpuHandle(const SrvManager *srvManager) const;

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
    void UpdateDepthStencilSrv();

    /// <summary>
    /// シーンカラーSRVの参照先を更新する
    /// </summary>
    void UpdateSceneColorSrv();

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

  private:
    static constexpr UINT kSwapChainBufferCount = 2;
    static constexpr UINT kSceneRtvIndex = kSwapChainBufferCount;
    static constexpr float kClearColor[4] = {0.030f, 0.026f, 0.055f, 1.0f};

    Microsoft::WRL::ComPtr<IDXGIFactory7> factory_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>
        commandAllocators_[kSwapChainBufferCount];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    bool isCommandListRecording_ = false;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> backBuffers_[kSwapChainBufferCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> sceneColorBuffer_;
    UINT sceneSrvIndex_ = UINT_MAX;
    UINT rtvDescriptorSize_ = 0;
    UINT backBufferIndex_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    UINT64 fenceValue_ = 0;
    UINT64 frameFenceValues_[kSwapChainBufferCount]{};
    HANDLE fenceEvent_ = nullptr;

    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissorRect_{};

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer_;
    SrvManager *srvManager_ = nullptr;
    UINT depthSrvIndex_ = UINT_MAX;
    D3D12_GPU_DESCRIPTOR_HANDLE depthSrvGpuHandle_{};
    D3D12_RESOURCE_STATES depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
};

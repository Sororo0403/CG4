#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

/// <summary>
/// Direct3D 12 のデバイスと描画フレーム管理を担う
/// </summary>
class DirectXCommon {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="hwnd">ウィンドウハンドル</param>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    void Initialize(HWND hwnd, int width, int height);

    /// <summary>
    /// フレーム開始処理
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// フレーム終了処理
    /// </summary>
    void EndFrame();

    /// <summary>
    /// 描画ターゲットと深度バッファを新しいサイズに合わせて再生成する
    /// </summary>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    void Resize(int width, int height);

    /// <summary>
    /// アップロード開始処理
    /// </summary>
    void BeginUpload();

    /// <summary>
    /// アップデート終了処理
    /// </summary>
    void EndUpload();

    /// <summary>
    /// GPU同期待ち
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
    /// スワップチェーンのバッファ数を取得する
    /// </summary>
    /// <returns>バックバッファ数</returns>
    UINT GetSwapChainBufferCount() const { return kSwapChainBufferCount; }

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
    /// GPU同期用フェンスを生成する
    /// </summary>
    void CreateFence();

  private:
    static constexpr UINT kSwapChainBufferCount = 2;
    static constexpr float kClearColor[4] = {0.1f, 0.2f, 0.4f, 1.0f};

    Microsoft::WRL::ComPtr<IDXGIFactory7> factory_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> backBuffers_[kSwapChainBufferCount];
    UINT rtvDescriptorSize_ = 0;
    UINT backBufferIndex_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    UINT64 fenceValue_ = 0;
    HANDLE fenceEvent_ = nullptr;

    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissorRect_{};

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer_;
};

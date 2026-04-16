#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

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

    // Getter
    ID3D12Device *GetDevice() const { return device_.Get(); }
    ID3D12CommandQueue *GetCommandQueue() const { return commandQueue_.Get(); }
    ID3D12GraphicsCommandList *GetCommandList() const {
        return commandList_.Get();
    }
    UINT GetSwapChainBufferCount() const { return kSwapChainBufferCount; }

  private:
    // Create
    void CreateFactory();
    void CreateDevice();
    void CreateCommandQueue();
    void CreateCommandAllocator();
    void CreateCommandList();
    void CreateSwapChain(HWND hwnd, int width, int height);
    void CreateRTV();
    void CreateViewport(int width, int height);
    void CreateScissor(int width, int height);
    void CreateDepthStencil(int width, int height);
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

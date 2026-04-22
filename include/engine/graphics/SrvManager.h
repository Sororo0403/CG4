#pragma once
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;

/// <summary>
/// SRVディスクリプタヒープの確保と参照を管理する
/// </summary>
class SrvManager {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="maxSrvCount">確保するSRV最大数</param>
    void Initialize(DirectXCommon *dxCommon, UINT maxSrvCount = 256);

    /// <summary>
    /// SRVを1つ割り当てる
    /// </summary>
    /// <returns>割り当てられたSRVインデックス</returns>
    UINT Allocate();

    /// <summary>
    /// 指定インデックスのCPUハンドルを取得する
    /// </summary>
    /// <param name="index">SRVインデックス</param>
    /// <returns>CPUディスクリプタハンドル</returns>
    D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(UINT index) const;
    /// <summary>
    /// 指定インデックスのGPUハンドルを取得する
    /// </summary>
    /// <param name="index">SRVインデックス</param>
    /// <returns>GPUディスクリプタハンドル</returns>
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(UINT index) const;
    /// <summary>
    /// SRVヒープを取得する
    /// </summary>
    /// <returns>ディスクリプタヒープ</returns>
    ID3D12DescriptorHeap *GetHeap() const { return heap_.Get(); }
    /// <summary>
    /// ディスクリプタサイズを取得する
    /// </summary>
    /// <returns>1ディスクリプタあたりのサイズ</returns>
    UINT GetDescriptorSize() const { return descriptorSize_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    UINT descriptorSize_ = 0;
    UINT currentIndex_ = 0;
};

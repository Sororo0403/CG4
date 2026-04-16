#pragma once
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;

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
    /// <returns>割り当てられたSRVindex/returns>
    UINT Allocate();

    // Getter
    D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(UINT index) const;
    ID3D12DescriptorHeap *GetHeap() const { return heap_.Get(); }
    UINT GetDescriptorSize() const { return descriptorSize_; }

  private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    UINT descriptorSize_ = 0;
    UINT currentIndex_ = 0;
};

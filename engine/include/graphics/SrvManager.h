#pragma once
#include "core/ResourceHandle.h"

#include <d3d12.h>
#include <memory>

class DirectXCommon;

/// <summary>
/// SRVディスクリプタヒープの確保と参照を管理する
/// </summary>
class SrvManager {
public:
    SrvManager();
    ~SrvManager();

    /// <summary>
    /// SRVディスクリプタヒープを指定数で初期化する
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="maxSrvCount">確保するSRV最大数</param>
    void Initialize(const DirectXCommon* dxCommon, UINT maxSrvCount = 4096);

    /// <summary>
    /// SRVを1つ割り当てる
    /// </summary>
    /// <returns>割り当てられたSRVインデックス</returns>
    UINT Allocate();
    UINT AllocateRange(UINT count);
    // Total free descriptors, not necessarily contiguous.
    bool CanAllocate(UINT count = 1) const {
        return CanAllocateDescriptors(count);
    }
    // Total free descriptors for callers that allocate slots one by one.
    bool CanAllocateDescriptors(UINT count = 1) const;
    // Contiguous free descriptors for AllocateRange callers.
    bool CanAllocateRange(UINT count) const;

    /// <summary>
    /// SRVを1つ割り当て、型付きハンドルで返す
    /// </summary>
    DescriptorHandle AllocateHandle() {
        return DescriptorHandle(Allocate());
    }

    /// <summary>
    /// 指定インデックスを解放して再利用可能にする
    /// </summary>
    void Free(UINT index);
    bool FreeIfAllocated(UINT index);
    bool IsAllocated(UINT index) const;

    /// <summary>
    /// 指定ハンドルを解放して再利用可能にする
    /// </summary>
    void Free(DescriptorHandle handle) {
        Free(handle.Get());
    }
    bool FreeIfAllocated(DescriptorHandle handle) {
        return FreeIfAllocated(handle.Get());
    }

    /// <summary>
    /// 指定インデックスのCPUハンドルを取得する
    /// </summary>
    /// <param name="index">SRVインデックス</param>
    /// <returns>CPUディスクリプタハンドル</returns>
    D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(UINT index) const;

    /// <summary>
    /// 指定ハンドルのCPUハンドルを取得する
    /// </summary>
    D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(DescriptorHandle handle) const {
        return GetCpuHandle(handle.Get());
    }

    /// <summary>
    /// 指定インデックスのGPUハンドルを取得する
    /// </summary>
    /// <param name="index">SRVインデックス</param>
    /// <returns>GPUディスクリプタハンドル</returns>
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(UINT index) const;

    /// <summary>
    /// 指定ハンドルのGPUハンドルを取得する
    /// </summary>
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(DescriptorHandle handle) const {
        return GetGpuHandle(handle.Get());
    }

    /// <summary>
    /// SRVヒープを取得する
    /// </summary>
    /// <returns>ディスクリプタヒープ</returns>
    ID3D12DescriptorHeap* GetHeap() const;

    /// <summary>
    /// ディスクリプタサイズを取得する
    /// </summary>
    /// <returns>1ディスクリプタあたりのサイズ</returns>
    UINT GetDescriptorSize() const;

private:
    struct State;

    UINT FindAvailableRange(UINT count) const;
    static void ValidateAllocatedIndex(UINT index, const char* operation);

    std::unique_ptr<State> state_;
};

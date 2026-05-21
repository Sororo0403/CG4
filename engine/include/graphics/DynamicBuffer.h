#pragma once

#include "graphics/UploadRingBuffer.h"

#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <cstring>
#include <wrl.h>

class DynamicBuffer {
  public:
    DynamicBuffer() = default;
    ~DynamicBuffer();
    DynamicBuffer(const DynamicBuffer &) = delete;
    DynamicBuffer &operator=(const DynamicBuffer &) = delete;
    DynamicBuffer(DynamicBuffer &&) = delete;
    DynamicBuffer &operator=(DynamicBuffer &&) = delete;

    void Initialize(ID3D12Device *device, size_t capacity,
                    size_t defaultAlignment = 256);
    void Reset();

    void BeginWrite();
    void Reserve(size_t capacity);

    UploadAllocation Allocate(size_t size, size_t alignment = 0);

    template <class T>
    UploadAllocation Write(const T &value, size_t alignment = 0) {
        UploadAllocation allocation = Allocate(sizeof(T), alignment);
        *static_cast<T *>(allocation.cpu) = value;
        return allocation;
    }

    template <class T>
    UploadAllocation WriteArray(const T *values, size_t count,
                                size_t alignment = 0) {
        UploadAllocation allocation =
            Allocate(sizeof(T) * count, alignment == 0 ? alignof(T) : alignment);
        if (values && count > 0) {
            std::memcpy(allocation.cpu, values, sizeof(T) * count);
        }
        return allocation;
    }

    ID3D12Resource *GetResource() const { return resource_.Get(); }
    D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress() const;
    size_t GetCapacity() const { return capacity_; }
    size_t GetOffset() const { return offset_; }
    size_t GetDefaultAlignment() const { return defaultAlignment_; }

  private:
    static size_t AlignUp(size_t value, size_t alignment);
    void CreateResource(size_t capacity);
    void UnmapResource();

    ID3D12Device *device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    uint8_t *mapped_ = nullptr;
    size_t capacity_ = 0;
    size_t offset_ = 0;
    size_t defaultAlignment_ = 256;
};

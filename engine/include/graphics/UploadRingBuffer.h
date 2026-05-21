#pragma once
#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <cstring>
#include <vector>
#include <wrl.h>

/// <summary>
/// UploadBufferから切り出したGPU転送用メモリ範囲
/// </summary>
struct UploadAllocation {
    void *cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
    size_t size = 0;
    size_t offset = 0;
    ID3D12Resource *resource = nullptr;
};

/// <summary>
/// フレームごとにリセットして使うUpload Heapのリングバッファ
/// </summary>
class UploadRingBuffer {
  public:
    UploadRingBuffer() = default;
    ~UploadRingBuffer();

    UploadRingBuffer(const UploadRingBuffer &) = delete;
    UploadRingBuffer &operator=(const UploadRingBuffer &) = delete;
    UploadRingBuffer(UploadRingBuffer &&) = delete;
    UploadRingBuffer &operator=(UploadRingBuffer &&) = delete;

    void Initialize(ID3D12Device *device, size_t bytesPerFrame,
                    uint32_t frameCount = 2);
    void Reset();

    void BeginFrame();
    void BeginFrame(uint32_t frameIndex);

    UploadAllocation Allocate(size_t size, size_t alignment = 256);

    template <class T>
    UploadAllocation Write(const T &value, size_t alignment = 256) {
        UploadAllocation allocation = Allocate(sizeof(T), alignment);
        *static_cast<T *>(allocation.cpu) = value;
        return allocation;
    }

    template <class T>
    UploadAllocation WriteArray(const T *values, size_t count,
                                size_t alignment = alignof(T)) {
        UploadAllocation allocation =
            Allocate(sizeof(T) * count, alignment);
        if (values && count > 0) {
            std::memcpy(allocation.cpu, values, sizeof(T) * count);
        }
        return allocation;
    }

    uint32_t GetFrameIndex() const { return frameIndex_; }
    size_t GetBytesPerFrame() const { return bytesPerFrame_; }
    size_t GetFrameOffset() const;

  private:
    struct FrameResource {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint8_t *mapped = nullptr;
        size_t offset = 0;
    };

    static size_t AlignUp(size_t value, size_t alignment);
    void CreateFrameResource(FrameResource &frame);
    void UnmapFrameResource(FrameResource &frame);

    ID3D12Device *device_ = nullptr;
    size_t bytesPerFrame_ = 0;
    uint32_t frameIndex_ = 0;
    std::vector<FrameResource> frames_;
};

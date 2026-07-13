#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <d3d12.h>
#include <limits>
#include <utility>
#include <vector>
#include <wrl.h>

/// <summary>
/// UploadBufferから切り出したGPU転送用メモリ範囲
/// </summary>
struct UploadAllocation {
    void* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
    size_t size = 0;
    size_t offset = 0;
    ID3D12Resource* resource = nullptr;
};

/// <summary>
/// フレームごとにリセットして使うUpload Heapのリングバッファ
/// </summary>
class UploadRingBuffer {
public:
    UploadRingBuffer() = default;
    ~UploadRingBuffer();

    UploadRingBuffer(const UploadRingBuffer&) = delete;
    UploadRingBuffer& operator=(const UploadRingBuffer&) = delete;
    UploadRingBuffer(UploadRingBuffer&&) = delete;
    UploadRingBuffer& operator=(UploadRingBuffer&&) = delete;

    void Initialize(ID3D12Device* device, size_t bytesPerFrame, uint32_t frameCount = 2);
    /// <summary>
    /// 状態をリセットする
    /// </summary>
    void Reset();

    void BeginFrame();
    /// <summary>
    /// Frameを開始する
    /// </summary>
    void BeginFrame(uint32_t frameIndex);

    UploadAllocation Allocate(size_t size, size_t alignment = 256);

    template <class T> UploadAllocation Write(const T& value, size_t alignment = 256) {
        UploadAllocation allocation = Allocate(sizeof(T), alignment);
        if (allocation.cpu != nullptr) {
            *static_cast<T*>(allocation.cpu) = value;
        }
        return allocation;
    }

    template <class T>
    UploadAllocation WriteArray(const T* values, size_t count, size_t alignment = alignof(T)) {
        if (!values || count == 0) {
            return {};
        }
        if (count > (std::numeric_limits<size_t>::max)() / sizeof(T)) {
            return {};
        }
        const size_t bytes = sizeof(T) * count;
        UploadAllocation allocation = Allocate(bytes, alignment);
        if (allocation.cpu != nullptr) {
            std::memcpy(allocation.cpu, values, bytes);
        }
        return allocation;
    }

    uint32_t GetFrameIndex() const {
        return frameIndex_;
    }
    size_t GetBytesPerFrame() const {
        return bytesPerFrame_;
    }
    size_t GetTotalBytes() const {
        return bytesPerFrame_ * frames_.size();
    }
    size_t GetFrameCount() const {
        return frames_.size();
    }
    bool HasResources() const noexcept;
    /// <summary>
    /// FrameOffsetを取得する
    /// </summary>
    size_t GetFrameOffset() const;

private:
    struct FrameResource {
        FrameResource() = default;
        ~FrameResource() {
            Reset();
        }
        FrameResource(const FrameResource&) = delete;
        FrameResource& operator=(const FrameResource&) = delete;
        FrameResource(FrameResource&& other) noexcept
            : resource(std::move(other.resource)), mapped(other.mapped), offset(other.offset) {
            other.mapped = nullptr;
            other.offset = 0;
        }
        FrameResource& operator=(FrameResource&& other) noexcept {
            if (this != &other) {
                Reset();
                resource = std::move(other.resource);
                mapped = other.mapped;
                offset = other.offset;
                other.mapped = nullptr;
                other.offset = 0;
            }
            return *this;
        }

        void Reset() {
            if (resource && mapped) {
                resource->Unmap(0, nullptr);
                mapped = nullptr;
            }
            resource.Reset();
            offset = 0;
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint8_t* mapped = nullptr;
        size_t offset = 0;
    };

    /// <summary>
    /// AlignUpを実行する
    /// </summary>
    static size_t AlignUp(size_t value, size_t alignment);
    static bool CreateFrameResource(FrameResource& frame, ID3D12Device* device,
                                    size_t bytesPerFrame);

    ID3D12Device* device_ = nullptr;
    size_t bytesPerFrame_ = 0;
    uint32_t frameIndex_ = 0;
    std::vector<FrameResource> frames_;
};

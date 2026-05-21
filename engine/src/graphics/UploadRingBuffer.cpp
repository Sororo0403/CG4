#include "graphics/UploadRingBuffer.h"

#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include <stdexcept>

using namespace DxUtils;

UploadRingBuffer::~UploadRingBuffer() { Reset(); }

void UploadRingBuffer::Initialize(ID3D12Device *device, size_t bytesPerFrame,
                                  uint32_t frameCount) {
    if (!device || bytesPerFrame == 0 || frameCount == 0) {
        throw std::runtime_error("UploadRingBuffer::Initialize invalid argument");
    }

    Reset();
    device_ = device;
    bytesPerFrame_ = AlignUp(bytesPerFrame, 256);
    frames_.resize(frameCount);
    for (FrameResource &frame : frames_) {
        CreateFrameResource(frame);
    }
}

void UploadRingBuffer::Reset() {
    for (FrameResource &frame : frames_) {
        UnmapFrameResource(frame);
        frame.resource.Reset();
        frame.offset = 0;
    }
    frames_.clear();
    device_ = nullptr;
    bytesPerFrame_ = 0;
    frameIndex_ = 0;
}

void UploadRingBuffer::BeginFrame() {
    if (frames_.empty()) {
        return;
    }
    frameIndex_ = (frameIndex_ + 1) % static_cast<uint32_t>(frames_.size());
    frames_[frameIndex_].offset = 0;
}

void UploadRingBuffer::BeginFrame(uint32_t frameIndex) {
    if (frames_.empty()) {
        return;
    }
    frameIndex_ = frameIndex % static_cast<uint32_t>(frames_.size());
    frames_[frameIndex_].offset = 0;
}

UploadAllocation UploadRingBuffer::Allocate(size_t size, size_t alignment) {
    if (frames_.empty() || size == 0) {
        throw std::runtime_error("UploadRingBuffer::Allocate before Initialize");
    }

    FrameResource &frame = frames_[frameIndex_];
    const size_t alignedOffset = AlignUp(frame.offset, alignment);
    const size_t endOffset = alignedOffset + size;
    if (endOffset > bytesPerFrame_) {
        throw std::runtime_error("UploadRingBuffer frame capacity exceeded");
    }

    frame.offset = endOffset;
    UploadAllocation allocation{};
    allocation.cpu = frame.mapped + alignedOffset;
    allocation.gpu = frame.resource->GetGPUVirtualAddress() + alignedOffset;
    allocation.size = size;
    allocation.offset = alignedOffset;
    allocation.resource = frame.resource.Get();
    return allocation;
}

size_t UploadRingBuffer::GetFrameOffset() const {
    if (frames_.empty()) {
        return 0;
    }
    return frames_[frameIndex_].offset;
}

size_t UploadRingBuffer::AlignUp(size_t value, size_t alignment) {
    if (alignment <= 1) {
        return value;
    }
    return ((value + alignment - 1) / alignment) * alignment;
}

void UploadRingBuffer::CreateFrameResource(FrameResource &frame) {
    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytesPerFrame_);
    ThrowIfFailed(device_->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&frame.resource)),
                  "CreateCommittedResource(UploadRingBuffer) failed");
    ThrowIfFailed(frame.resource->Map(
                      0, nullptr, reinterpret_cast<void **>(&frame.mapped)),
                  "Map(UploadRingBuffer) failed");
    frame.offset = 0;
}

void UploadRingBuffer::UnmapFrameResource(FrameResource &frame) {
    if (frame.resource && frame.mapped) {
        frame.resource->Unmap(0, nullptr);
        frame.mapped = nullptr;
    }
}

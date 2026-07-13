#include "graphics/UploadRingBuffer.h"

#include "core/Alignment.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"

#include <algorithm>
#include <cassert>
#include <exception>
#include <limits>
#include <new>
#include <utility>

UploadRingBuffer::~UploadRingBuffer() {
    if (HasResources()) {
        assert(false && "UploadRingBuffer::Reset must be called after GPU idle before "
                        "destruction");
    }
    Reset();
}

void UploadRingBuffer::Initialize(ID3D12Device* device, size_t bytesPerFrame, uint32_t frameCount) {
    if (!device || bytesPerFrame == 0 || frameCount == 0) {
        Reset();
        return;
    }

    Reset();
    const size_t alignedBytesPerFrame = AlignUp(bytesPerFrame, 256);
    if (alignedBytesPerFrame == 0 || alignedBytesPerFrame == (std::numeric_limits<size_t>::max)()) {
        Reset();
        return;
    }

    std::vector<FrameResource> newFrames;
    try {
        newFrames.resize(frameCount);
    } catch (const std::exception&) {
        Reset();
        return;
    }
    for (FrameResource& frame : newFrames) {
        if (!CreateFrameResource(frame, device, alignedBytesPerFrame)) {
            Reset();
            return;
        }
    }
    device_ = device;
    bytesPerFrame_ = alignedBytesPerFrame;
    frames_ = std::move(newFrames);
    frameIndex_ = 0;
}

void UploadRingBuffer::Reset() {
    for (FrameResource& frame : frames_) {
        frame.Reset();
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
        return {};
    }

    FrameResource& frame = frames_[frameIndex_];
    if (!frame.resource || frame.mapped == nullptr) {
        return {};
    }

    const size_t alignedOffset = AlignUp(frame.offset, alignment);
    if (size > (std::numeric_limits<size_t>::max)() - alignedOffset) {
        return {};
    }
    const size_t endOffset = alignedOffset + size;
    if (endOffset > bytesPerFrame_) {
        return {};
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
    return CoreAlignment::AlignUp(value, alignment);
}

bool UploadRingBuffer::CreateFrameResource(FrameResource& frame, ID3D12Device* device,
                                           size_t bytesPerFrame) {
    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytesPerFrame);
    if (!GpuResourceHelpers::CreateCommittedResourceChecked(
            device, &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            frame.resource.GetAddressOf())) {
        frame.Reset();
        return false;
    }
    frame.resource->SetName(L"UploadRingBuffer.FrameResource");
    if (!GpuResourceHelpers::MapResourceChecked(frame.resource.Get(), &frame.mapped)) {
        frame.Reset();
        return false;
    }
    frame.offset = 0;
    return true;
}

bool UploadRingBuffer::HasResources() const noexcept {
    return std::any_of(frames_.begin(), frames_.end(),
                       [](const FrameResource& frame) { return frame.resource != nullptr; });
}

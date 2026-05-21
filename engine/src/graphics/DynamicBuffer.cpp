#include "graphics/DynamicBuffer.h"

#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"

#include <stdexcept>

using namespace DxUtils;

DynamicBuffer::~DynamicBuffer() { Reset(); }

void DynamicBuffer::Initialize(ID3D12Device *device, size_t capacity,
                               size_t defaultAlignment) {
    if (!device || capacity == 0) {
        throw std::runtime_error("DynamicBuffer::Initialize invalid argument");
    }

    Reset();
    device_ = device;
    defaultAlignment_ = defaultAlignment == 0 ? 1 : defaultAlignment;
    CreateResource(capacity);
}

void DynamicBuffer::Reset() {
    UnmapResource();
    resource_.Reset();
    device_ = nullptr;
    capacity_ = 0;
    offset_ = 0;
    defaultAlignment_ = 256;
}

void DynamicBuffer::BeginWrite() { offset_ = 0; }

void DynamicBuffer::Reserve(size_t capacity) {
    if (!device_) {
        throw std::runtime_error("DynamicBuffer::Reserve before Initialize");
    }
    if (capacity <= capacity_) {
        return;
    }
    if (offset_ != 0) {
        throw std::runtime_error(
            "DynamicBuffer::Reserve cannot grow after allocations");
    }
    UnmapResource();
    resource_.Reset();
    CreateResource(capacity);
}

UploadAllocation DynamicBuffer::Allocate(size_t size, size_t alignment) {
    if (!resource_ || size == 0) {
        throw std::runtime_error("DynamicBuffer::Allocate before Initialize");
    }

    const size_t effectiveAlignment =
        alignment == 0 ? defaultAlignment_ : alignment;
    const size_t alignedOffset = AlignUp(offset_, effectiveAlignment);
    const size_t endOffset = alignedOffset + size;
    if (endOffset > capacity_) {
        throw std::runtime_error("DynamicBuffer capacity exceeded");
    }

    offset_ = endOffset;
    UploadAllocation allocation{};
    allocation.cpu = mapped_ + alignedOffset;
    allocation.gpu = resource_->GetGPUVirtualAddress() + alignedOffset;
    allocation.size = size;
    allocation.offset = alignedOffset;
    allocation.resource = resource_.Get();
    return allocation;
}

D3D12_GPU_VIRTUAL_ADDRESS DynamicBuffer::GetGpuVirtualAddress() const {
    if (!resource_) {
        return 0;
    }
    return resource_->GetGPUVirtualAddress();
}

size_t DynamicBuffer::AlignUp(size_t value, size_t alignment) {
    if (alignment <= 1) {
        return value;
    }
    return ((value + alignment - 1) / alignment) * alignment;
}

void DynamicBuffer::CreateResource(size_t capacity) {
    capacity_ = AlignUp(capacity, defaultAlignment_);
    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(capacity_);
    ThrowIfFailed(device_->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&resource_)),
                  "CreateCommittedResource(DynamicBuffer) failed");
    ThrowIfFailed(resource_->Map(0, nullptr, reinterpret_cast<void **>(&mapped_)),
                  "Map(DynamicBuffer) failed");
    offset_ = 0;
}

void DynamicBuffer::UnmapResource() {
    if (resource_ && mapped_) {
        resource_->Unmap(0, nullptr);
        mapped_ = nullptr;
    }
}

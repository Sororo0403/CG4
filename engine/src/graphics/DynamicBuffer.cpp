#include "graphics/DynamicBuffer.h"

#include "graphics/DxHelpers.h"

#include <limits>
#include <utility>

DynamicBuffer::~DynamicBuffer() { Reset(); }

void DynamicBuffer::Initialize(ID3D12Device *device, size_t capacity,
                               size_t defaultAlignment) {
    if (!device || capacity == 0) {
        Reset();
        return;
    }

    Reset();
    device_ = device;
    defaultAlignment_ = defaultAlignment == 0 ? 1 : defaultAlignment;
    if (!CreateResource(capacity)) {
        Reset();
    }
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
        return;
    }
    if (capacity <= capacity_) {
        return;
    }
    if (offset_ != 0) {
        return;
    }
    CreateResource(capacity);
}

UploadAllocation DynamicBuffer::Allocate(size_t size, size_t alignment) {
    if (!resource_ || size == 0) {
        return {};
    }

    const size_t effectiveAlignment =
        alignment == 0 ? defaultAlignment_ : alignment;
    const size_t alignedOffset = AlignUp(offset_, effectiveAlignment);
    if (size > (std::numeric_limits<size_t>::max)() - alignedOffset) {
        return {};
    }
    const size_t endOffset = alignedOffset + size;
    if (endOffset > capacity_) {
        return {};
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
    const size_t addend = alignment - 1;
    if (value > (std::numeric_limits<size_t>::max)() - addend) {
        return (std::numeric_limits<size_t>::max)();
    }
    return ((value + addend) / alignment) * alignment;
}

bool DynamicBuffer::CreateResource(size_t capacity) {
    const size_t alignedCapacity = AlignUp(capacity, defaultAlignment_);
    if (alignedCapacity == 0 ||
        alignedCapacity == (std::numeric_limits<size_t>::max)()) {
        return false;
    }
    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(alignedCapacity);
    Microsoft::WRL::ComPtr<ID3D12Resource> newResource;
    uint8_t *newMapped = nullptr;
    const HRESULT resourceResult = device_->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&newResource));
    if (FAILED(resourceResult) || !newResource) {
        return false;
    }
    const HRESULT mapResult =
        newResource->Map(0, nullptr, reinterpret_cast<void **>(&newMapped));
    if (FAILED(mapResult) || newMapped == nullptr) {
        return false;
    }
    UnmapResource();
    resource_.Reset();
    resource_ = std::move(newResource);
    mapped_ = newMapped;
    capacity_ = alignedCapacity;
    offset_ = 0;
    return true;
}

void DynamicBuffer::UnmapResource() {
    if (resource_ && mapped_) {
        resource_->Unmap(0, nullptr);
        mapped_ = nullptr;
    }
}

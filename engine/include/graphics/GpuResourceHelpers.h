#pragma once

#include <d3d12.h>

namespace GpuResourceHelpers {

inline bool CreateCommittedResourceChecked(
    ID3D12Device* device, const D3D12_HEAP_PROPERTIES* heapProperties, D3D12_HEAP_FLAGS heapFlags,
    const D3D12_RESOURCE_DESC* resourceDesc, D3D12_RESOURCE_STATES initialState,
    const D3D12_CLEAR_VALUE* clearValue, ID3D12Resource** resource) {
    if (device == nullptr || heapProperties == nullptr || resourceDesc == nullptr ||
        resource == nullptr) {
        return false;
    }
    *resource = nullptr;
    return SUCCEEDED(device->CreateCommittedResource(heapProperties, heapFlags, resourceDesc,
                                                     initialState, clearValue,
                                                     IID_PPV_ARGS(resource))) &&
           *resource != nullptr;
}

inline bool CreateCommittedResourceChecked(ID3D12Device* device,
                                           const D3D12_HEAP_PROPERTIES* heapProperties,
                                           D3D12_HEAP_FLAGS heapFlags,
                                           const D3D12_RESOURCE_DESC* resourceDesc,
                                           D3D12_RESOURCE_STATES initialState,
                                           ID3D12Resource** resource) {
    return CreateCommittedResourceChecked(device, heapProperties, heapFlags, resourceDesc,
                                          initialState, nullptr, resource);
}

inline bool MapResourceChecked(ID3D12Resource* resource, void** mapped) {
    if (resource == nullptr || mapped == nullptr) {
        return false;
    }
    *mapped = nullptr;
    return SUCCEEDED(resource->Map(0, nullptr, mapped)) && *mapped != nullptr;
}

template <typename T> inline bool MapResourceChecked(ID3D12Resource* resource, T** mapped) {
    if (mapped == nullptr) {
        return false;
    }
    void* raw = nullptr;
    if (!MapResourceChecked(resource, &raw)) {
        *mapped = nullptr;
        return false;
    }
    *mapped = static_cast<T*>(raw);
    return true;
}

} // namespace GpuResourceHelpers

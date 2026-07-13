#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

struct MeshInstanceBuffer {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource;
    D3D12_VERTEX_BUFFER_VIEW view{};
    uint32_t instanceCount = 0;
    uint64_t contentHash = 0;
    mutable UINT lastUsedFrameIndex = UINT_MAX;

    bool IsValid() const {
        return resource && view.BufferLocation != 0 && view.SizeInBytes > 0 &&
               view.StrideInBytes > 0 && instanceCount > 0;
    }

    void Reset() {
        resource.Reset();
        uploadResource.Reset();
        view = {};
        instanceCount = 0;
        contentHash = 0;
        lastUsedFrameIndex = UINT_MAX;
    }
};

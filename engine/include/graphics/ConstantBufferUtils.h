#pragma once

#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"

#include <algorithm>
#include <cstddef>
#include <d3d12.h>
#include <exception>
#include <limits>
#include <vector>
#include <wrl/client.h>

namespace ConstantBufferUtils {

inline UINT Align256(size_t size) {
    if (size > static_cast<size_t>((std::numeric_limits<UINT>::max)()) - 0xFFu) {
        return 0;
    }
    return static_cast<UINT>((size + 0xFFu) & ~size_t{0xFFu});
}

template <typename Frame, typename Data>
inline bool CreateUploadFrames(ID3D12Device* device, UINT frameCount, size_t elementSize,
                               std::vector<Frame>& frames,
                               Microsoft::WRL::ComPtr<ID3D12Resource> Frame::* resourceMember,
                               Data* Frame::* mappedMember) {
    const UINT bufferSize = Align256(elementSize);
    if (device == nullptr || frameCount == 0u || bufferSize == 0u) {
        return false;
    }

    auto resetFrames = [&]() {
        for (Frame& frame : frames) {
            auto& resource = frame.*resourceMember;
            auto& mapped = frame.*mappedMember;
            if (resource && mapped != nullptr) {
                resource->Unmap(0, nullptr);
            }
            mapped = nullptr;
            resource.Reset();
        }
        frames.clear();
    };

    resetFrames();

    try {
        frames.resize(frameCount);
    } catch (const std::exception&) {
        resetFrames();
        return false;
    }

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    for (Frame& frame : frames) {
        auto& resource = frame.*resourceMember;
        auto& mapped = frame.*mappedMember;
        if (!GpuResourceHelpers::CreateCommittedResourceChecked(
                device, &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                resource.GetAddressOf())) {
            resetFrames();
            return false;
        }
        if (!GpuResourceHelpers::MapResourceChecked(resource.Get(), &mapped)) {
            resetFrames();
            return false;
        }
    }
    return true;
}

template <typename Frame, typename Data>
inline bool HasMappedFrames(const std::vector<Frame>& frames,
                            Microsoft::WRL::ComPtr<ID3D12Resource> Frame::* resourceMember,
                            Data* Frame::* mappedMember) {
    if (frames.empty()) {
        return false;
    }
    return std::all_of(frames.begin(), frames.end(), [&](const Frame& frame) {
        const auto& resource = frame.*resourceMember;
        const auto mapped = frame.*mappedMember;
        return resource && mapped != nullptr;
    });
}

} // namespace ConstantBufferUtils

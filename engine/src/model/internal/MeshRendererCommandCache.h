#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d12.h>

struct MeshRendererCommandCache {
    static constexpr size_t kRootParameterCount = 13;
    static constexpr size_t kVertexBufferViewCount = 2;

    enum class RootParameterKind : uint8_t {
        None,
        ConstantBuffer,
        DescriptorTable,
        ShaderResource,
    };

    ID3D12RootSignature* rootSignature = nullptr;
    ID3D12PipelineState* pipelineState = nullptr;
    std::array<RootParameterKind, kRootParameterCount> rootParameterKinds{};
    std::array<uint64_t, kRootParameterCount> rootParameterValues{};
    std::array<D3D12_VERTEX_BUFFER_VIEW, kVertexBufferViewCount> vertexBufferViews{};
    uint32_t vertexBufferStartSlot = 0;
    uint32_t vertexBufferViewCount = 0;
    bool vertexBuffersValid = false;
    D3D12_INDEX_BUFFER_VIEW indexBufferView{};
    bool indexBufferValid = false;
    D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

    static bool SameVertexBufferView(const D3D12_VERTEX_BUFFER_VIEW& lhs,
                                     const D3D12_VERTEX_BUFFER_VIEW& rhs) noexcept {
        return lhs.BufferLocation == rhs.BufferLocation && lhs.SizeInBytes == rhs.SizeInBytes &&
               lhs.StrideInBytes == rhs.StrideInBytes;
    }

    static bool SameIndexBufferView(const D3D12_INDEX_BUFFER_VIEW& lhs,
                                    const D3D12_INDEX_BUFFER_VIEW& rhs) noexcept {
        return lhs.BufferLocation == rhs.BufferLocation && lhs.SizeInBytes == rhs.SizeInBytes &&
               lhs.Format == rhs.Format;
    }

    void Reset() noexcept {
        rootSignature = nullptr;
        pipelineState = nullptr;
        rootParameterKinds.fill(RootParameterKind::None);
        rootParameterValues.fill(0);
        vertexBufferViews = {};
        vertexBufferStartSlot = 0;
        vertexBufferViewCount = 0;
        vertexBuffersValid = false;
        indexBufferView = {};
        indexBufferValid = false;
        primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    }
};

#pragma once

#include <array>
#include <cstddef>

#include <d3d12.h>

namespace RendererInputLayouts {

template <std::size_t N>
inline D3D12_INPUT_LAYOUT_DESC MakeDesc(
    const std::array<D3D12_INPUT_ELEMENT_DESC, N> &elements) {
    return {elements.data(), static_cast<UINT>(elements.size())};
}

inline constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 10> kMeshVertex{{
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
     D3D12_APPEND_ALIGNED_ELEMENT,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
     D3D12_APPEND_ALIGNED_ELEMENT,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
     D3D12_APPEND_ALIGNED_ELEMENT,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
     D3D12_APPEND_ALIGNED_ELEMENT,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"CUSTOMSCALAR", 0, DXGI_FORMAT_R32_FLOAT, 0,
     D3D12_APPEND_ALIGNED_ELEMENT,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"CUSTOMPOSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
     D3D12_APPEND_ALIGNED_ELEMENT,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"CUSTOMVECTOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
     D3D12_APPEND_ALIGNED_ELEMENT,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"CUSTOMPOSITION", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0,
     D3D12_APPEND_ALIGNED_ELEMENT,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"CUSTOMVECTOR", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
     D3D12_APPEND_ALIGNED_ELEMENT,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
}};

inline constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 7> kInstanceData{{
    {"WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,
     D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    {"WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
     D3D12_APPEND_ALIGNED_ELEMENT,
     D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    {"WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
     D3D12_APPEND_ALIGNED_ELEMENT,
     D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    {"WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
     D3D12_APPEND_ALIGNED_ELEMENT,
     D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    {"INSTANCECOLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
     D3D12_APPEND_ALIGNED_ELEMENT,
     D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    {"INSTANCEFADE", 0, DXGI_FORMAT_R32_FLOAT, 1,
     D3D12_APPEND_ALIGNED_ELEMENT,
     D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    {"INSTANCECUSTOMID", 0, DXGI_FORMAT_R32_UINT, 1,
     D3D12_APPEND_ALIGNED_ELEMENT,
     D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
}};

inline constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 17> kMeshInstanced{{
    kMeshVertex[0],
    kMeshVertex[1],
    kMeshVertex[2],
    kMeshVertex[3],
    kMeshVertex[4],
    kMeshVertex[5],
    kMeshVertex[6],
    kMeshVertex[7],
    kMeshVertex[8],
    kMeshVertex[9],
    kInstanceData[0],
    kInstanceData[1],
    kInstanceData[2],
    kInstanceData[3],
    kInstanceData[4],
    kInstanceData[5],
    kInstanceData[6],
}};

inline constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 7> kModelVertex{{
    kMeshVertex[0],
    kMeshVertex[1],
    kMeshVertex[2],
    kMeshVertex[3],
    kMeshVertex[4],
    kMeshVertex[5],
    kMeshVertex[6],
}};

inline constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 14> kModelInstanced{{
    kModelVertex[0],
    kModelVertex[1],
    kModelVertex[2],
    kModelVertex[3],
    kModelVertex[4],
    kModelVertex[5],
    kModelVertex[6],
    kInstanceData[0],
    kInstanceData[1],
    kInstanceData[2],
    kInstanceData[3],
    kInstanceData[4],
    kInstanceData[5],
    kInstanceData[6],
}};

} // namespace RendererInputLayouts

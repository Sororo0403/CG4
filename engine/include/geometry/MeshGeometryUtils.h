#pragma once

#include "geometry/MeshGeometry.h"

#include <cstdint>

namespace MeshGeometryUtils {

template <typename Geometry>
inline void AddTriangleIndices(Geometry& geometry, uint32_t a, uint32_t b, uint32_t c) {
    geometry.indices.insert(geometry.indices.end(), {a, b, c});
}

template <typename Geometry>
inline void AddQuadIndices(Geometry& geometry, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    geometry.indices.insert(geometry.indices.end(), {a, b, c, a, c, d});
}

template <typename Geometry>
inline void AddReversedQuadIndices(Geometry& geometry, uint32_t a, uint32_t b, uint32_t c,
                                   uint32_t d) {
    geometry.indices.insert(geometry.indices.end(), {a, c, b, b, c, d});
}

template <typename Geometry>
inline void AppendMeshGeometry(Geometry& target, const Geometry& source) {
    const uint32_t vertexOffset = static_cast<uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(), source.vertices.begin(), source.vertices.end());
    target.indices.reserve(target.indices.size() + source.indices.size());
    for (uint32_t index : source.indices) {
        target.indices.push_back(vertexOffset + index);
    }
}

} // namespace MeshGeometryUtils

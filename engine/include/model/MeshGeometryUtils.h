#pragma once

#include "model/MeshGeometry.h"

#include <cstdint>

namespace MeshGeometryUtils {

inline void AddTriangleIndices(MeshGeometry &geometry, uint32_t a, uint32_t b,
                               uint32_t c) {
    geometry.indices.insert(geometry.indices.end(), {a, b, c});
}

inline void AddQuadIndices(MeshGeometry &geometry, uint32_t a, uint32_t b,
                           uint32_t c, uint32_t d) {
    geometry.indices.insert(geometry.indices.end(), {a, b, c, a, c, d});
}

inline void AddReversedQuadIndices(MeshGeometry &geometry, uint32_t a,
                                   uint32_t b, uint32_t c, uint32_t d) {
    geometry.indices.insert(geometry.indices.end(), {a, c, b, b, c, d});
}

inline void AppendMeshGeometry(MeshGeometry &target,
                               const MeshGeometry &source) {
    const uint32_t vertexOffset = static_cast<uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(), source.vertices.begin(),
                           source.vertices.end());
    target.indices.reserve(target.indices.size() + source.indices.size());
    for (uint32_t index : source.indices) {
        target.indices.push_back(vertexOffset + index);
    }
}

} // namespace MeshGeometryUtils

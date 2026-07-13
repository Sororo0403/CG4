#pragma once

#include "core/ResourceHandle.h"

#include <DirectXMath.h>
#include <assimp/matrix4x4.h>
#include <cstdint>
#include <limits>

namespace AssimpMeshLoaderUtils {

inline DirectX::XMFLOAT4X4 ToMatrix(const aiMatrix4x4& m) {
    return {m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2,
            m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4};
}

inline uint32_t CheckedUint32Size(size_t value, const char* message) {
    (void)message;
    if (value > (std::numeric_limits<uint32_t>::max)()) {
        return kInvalidResourceId;
    }
    return static_cast<uint32_t>(value);
}

inline int CheckedIntSize(size_t value, const char* message) {
    (void)message;
    if (value > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return (std::numeric_limits<int>::max)();
    }
    return static_cast<int>(value);
}

} // namespace AssimpMeshLoaderUtils

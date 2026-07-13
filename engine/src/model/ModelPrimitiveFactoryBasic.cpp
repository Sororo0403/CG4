#include "core/Numeric.h"
#include "internal/ModelPrimitiveFactoryInternal.h"

#include <algorithm>
#include <array>
#include <exception>

using namespace DirectX;

namespace ModelPrimitiveFactory {
namespace {

using Internal::PrepareMaterial;
using Internal::ReserveProceduralMesh;
using Numeric::AtLeastFinite;

constexpr std::array<ModelVertex, 4> kPlaneVertices = {{
    {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
}};

constexpr std::array<uint32_t, 6> kPlaneIndices = {0, 1, 2, 2, 1, 3};

XMFLOAT3 Subtract(const XMFLOAT3& lhs, const XMFLOAT3& rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

XMFLOAT3 Cross(const XMFLOAT3& lhs, const XMFLOAT3& rhs) {
    return {lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x};
}

float Dot(const XMFLOAT3& lhs, const XMFLOAT3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

bool IsClockwiseFront(const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c,
                      const XMFLOAT3& normal) {
    const XMFLOAT3 ab = Subtract(b, a);
    const XMFLOAT3 ac = Subtract(c, a);
    return Dot(Cross(ab, ac), normal) > 0.0f;
}

void PushTriangle(std::vector<uint32_t>& indices, const uint32_t a, const uint32_t b,
                  const uint32_t c, const XMFLOAT3& normal,
                  const std::vector<ModelVertex>& vertices) {
    if (IsClockwiseFront(vertices[a].position, vertices[b].position, vertices[c].position,
                         normal)) {
        indices.push_back(a);
        indices.push_back(b);
        indices.push_back(c);
    } else {
        indices.push_back(a);
        indices.push_back(c);
        indices.push_back(b);
    }
}

} // namespace

std::optional<PrimitiveMeshData> BuildPlane(uint32_t textureId, const Material& material) {
    PrimitiveMeshData data{};
    data.material = PrepareMaterial(textureId, material);
    try {
        data.vertices.assign(kPlaneVertices.begin(), kPlaneVertices.end());
        data.indices.assign(kPlaneIndices.begin(), kPlaneIndices.end());
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return data;
}

std::optional<PrimitiveMeshData> BuildBox(uint32_t textureId, const Material& material, float width,
                                          float height, float depth) {
    width = AtLeastFinite(width, 0.001f, 0.001f);
    height = AtLeastFinite(height, 0.001f, 0.001f);
    depth = AtLeastFinite(depth, 0.001f, 0.001f);

    PrimitiveMeshData data{};
    data.material = PrepareMaterial(textureId, material);
    if (!ReserveProceduralMesh(data.vertices, data.indices, 24u, 36u)) {
        return std::nullopt;
    }

    const float hx = width * 0.5f;
    const float hz = depth * 0.5f;
    const float y0 = 0.0f;
    const float y1 = height;

    auto addFace = [&](const XMFLOAT3& normal, const XMFLOAT3& bottomLeft, const XMFLOAT3& topLeft,
                       const XMFLOAT3& bottomRight, const XMFLOAT3& topRight) {
        const uint32_t base = static_cast<uint32_t>(data.vertices.size());
        data.vertices.push_back({bottomLeft, normal, {0.0f, 1.0f}});
        data.vertices.push_back({topLeft, normal, {0.0f, 0.0f}});
        data.vertices.push_back({bottomRight, normal, {1.0f, 1.0f}});
        data.vertices.push_back({topRight, normal, {1.0f, 0.0f}});
        PushTriangle(data.indices, base + 0u, base + 1u, base + 2u, normal, data.vertices);
        PushTriangle(data.indices, base + 2u, base + 1u, base + 3u, normal, data.vertices);
    };

    try {
        addFace({0.0f, 0.0f, 1.0f}, {-hx, y0, hz}, {-hx, y1, hz}, {hx, y0, hz}, {hx, y1, hz});
        addFace({0.0f, 0.0f, -1.0f}, {hx, y0, -hz}, {hx, y1, -hz}, {-hx, y0, -hz}, {-hx, y1, -hz});
        addFace({1.0f, 0.0f, 0.0f}, {hx, y0, hz}, {hx, y1, hz}, {hx, y0, -hz}, {hx, y1, -hz});
        addFace({-1.0f, 0.0f, 0.0f}, {-hx, y0, -hz}, {-hx, y1, -hz}, {-hx, y0, hz}, {-hx, y1, hz});
        addFace({0.0f, 1.0f, 0.0f}, {-hx, y1, -hz}, {-hx, y1, hz}, {hx, y1, -hz}, {hx, y1, hz});
        addFace({0.0f, -1.0f, 0.0f}, {-hx, y0, hz}, {-hx, y0, -hz}, {hx, y0, hz}, {hx, y0, -hz});
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return data;
}

} // namespace ModelPrimitiveFactory

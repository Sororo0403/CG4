#include "internal/ModelPrimitiveFactoryInternal.h"

#include "core/Numeric.h"

#include <algorithm>
#include <array>

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

} // namespace

std::optional<PrimitiveMeshData> BuildPlane(uint32_t textureId,
                                            const Material &material) {
    PrimitiveMeshData data{};
    data.material = PrepareMaterial(textureId, material);
    data.vertices.assign(kPlaneVertices.begin(), kPlaneVertices.end());
    data.indices.assign(kPlaneIndices.begin(), kPlaneIndices.end());
    return data;
}

std::optional<PrimitiveMeshData> BuildBox(uint32_t textureId,
                                          const Material &material,
                                          float width,
                                          float height,
                                          float depth) {
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

    auto addFace = [&](const XMFLOAT3 &normal, const XMFLOAT3 &bottomLeft,
                       const XMFLOAT3 &topLeft, const XMFLOAT3 &bottomRight,
                       const XMFLOAT3 &topRight) {
        const uint32_t base = static_cast<uint32_t>(data.vertices.size());
        data.vertices.push_back({bottomLeft, normal, {0.0f, 1.0f}});
        data.vertices.push_back({topLeft, normal, {0.0f, 0.0f}});
        data.vertices.push_back({bottomRight, normal, {1.0f, 1.0f}});
        data.vertices.push_back({topRight, normal, {1.0f, 0.0f}});
        data.indices.push_back(base + 0u);
        data.indices.push_back(base + 1u);
        data.indices.push_back(base + 2u);
        data.indices.push_back(base + 2u);
        data.indices.push_back(base + 1u);
        data.indices.push_back(base + 3u);
    };

    addFace({0.0f, 0.0f, 1.0f}, {-hx, y0, hz}, {-hx, y1, hz}, {hx, y0, hz},
            {hx, y1, hz});
    addFace({0.0f, 0.0f, -1.0f}, {hx, y0, -hz}, {hx, y1, -hz},
            {-hx, y0, -hz}, {-hx, y1, -hz});
    addFace({1.0f, 0.0f, 0.0f}, {hx, y0, hz}, {hx, y1, hz}, {hx, y0, -hz},
            {hx, y1, -hz});
    addFace({-1.0f, 0.0f, 0.0f}, {-hx, y0, -hz}, {-hx, y1, -hz},
            {-hx, y0, hz}, {-hx, y1, hz});
    addFace({0.0f, 1.0f, 0.0f}, {-hx, y1, -hz}, {-hx, y1, hz},
            {hx, y1, -hz}, {hx, y1, hz});
    addFace({0.0f, -1.0f, 0.0f}, {-hx, y0, hz}, {-hx, y0, -hz},
            {hx, y0, hz}, {hx, y0, -hz});
    return data;
}

} // namespace ModelPrimitiveFactory

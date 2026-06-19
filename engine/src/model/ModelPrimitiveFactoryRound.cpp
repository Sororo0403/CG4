#include "internal/ModelPrimitiveFactoryInternal.h"

#include "core/Numeric.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace DirectX;

namespace ModelPrimitiveFactory {
namespace {

using Internal::CheckedMultiplySize;
using Internal::ClampProceduralSegments;
using Internal::kMaxProceduralSegments;
using Internal::PrepareMaterial;
using Internal::ReserveProceduralMesh;
using Numeric::AtLeastFinite;
using Numeric::ClampFinite;

struct CircleSegment {
    float sin0 = 0.0f;
    float cos0 = 1.0f;
    float sin1 = 0.0f;
    float cos1 = 1.0f;
    float u0 = 0.0f;
    float u1 = 0.0f;
};

CircleSegment BuildCircleSegment(uint32_t index, uint32_t divide) {
    const float radianPerDivide =
        std::numbers::pi_v<float> * 2.0f / static_cast<float>(divide);
    const float angle = static_cast<float>(index) * radianPerDivide;
    const float angleNext = static_cast<float>(index + 1u) * radianPerDivide;
    return {std::sin(angle),
            std::cos(angle),
            std::sin(angleNext),
            std::cos(angleNext),
            static_cast<float>(index) / static_cast<float>(divide),
            static_cast<float>(index + 1u) / static_cast<float>(divide)};
}

} // namespace

std::optional<PrimitiveMeshData> BuildSphere(uint32_t textureId,
                                             const Material &material,
                                             uint32_t slice,
                                             uint32_t stack,
                                             float radius) {
    slice = ClampProceduralSegments(slice, 3u, kMaxProceduralSegments);
    stack = ClampProceduralSegments(stack, 2u, kMaxProceduralSegments);
    radius = AtLeastFinite(radius, 0.001f, 0.001f);

    PrimitiveMeshData data{};
    data.material = PrepareMaterial(textureId, material);
    size_t sphereVertexCount = 0;
    size_t sphereIndexCount = 0;
    if (!CheckedMultiplySize(static_cast<size_t>(slice + 1u),
                             static_cast<size_t>(stack + 1u),
                             sphereVertexCount) ||
        !CheckedMultiplySize(static_cast<size_t>(slice),
                             static_cast<size_t>(stack),
                             sphereIndexCount) ||
        !CheckedMultiplySize(sphereIndexCount, 6u, sphereIndexCount) ||
        !ReserveProceduralMesh(data.vertices, data.indices, sphereVertexCount,
                               sphereIndexCount)) {
        return std::nullopt;
    }

    constexpr float pi = std::numbers::pi_v<float>;
    for (uint32_t y = 0; y <= stack; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(stack);
        const float pitch = v * pi;
        const float sinPitch = std::sinf(pitch);
        const float cosPitch = std::cosf(pitch);
        for (uint32_t x = 0; x <= slice; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(slice);
            const float yaw = u * pi * 2.0f;
            XMFLOAT3 normal{std::sinf(yaw) * sinPitch, cosPitch,
                            std::cosf(yaw) * sinPitch};
            XMFLOAT3 position{normal.x * radius, normal.y * radius,
                              normal.z * radius};
            data.vertices.push_back({position, normal, {u, v}});
        }
    }

    const uint32_t row = slice + 1u;
    for (uint32_t y = 0; y < stack; ++y) {
        for (uint32_t x = 0; x < slice; ++x) {
            const uint32_t i0 = y * row + x;
            const uint32_t i1 = i0 + 1u;
            const uint32_t i2 = i0 + row;
            const uint32_t i3 = i2 + 1u;
            data.indices.push_back(i0);
            data.indices.push_back(i1);
            data.indices.push_back(i2);
            data.indices.push_back(i2);
            data.indices.push_back(i1);
            data.indices.push_back(i3);
        }
    }
    return data;
}

std::optional<PrimitiveMeshData> BuildRing(uint32_t textureId,
                                           const Material &material,
                                           uint32_t divide,
                                           float outerRadius,
                                           float innerRadius) {
    divide = ClampProceduralSegments(divide, 3u, kMaxProceduralSegments);
    outerRadius = AtLeastFinite(outerRadius, 0.001f, 0.001f);
    innerRadius =
        ClampFinite(innerRadius, 0.0f, outerRadius - 0.0001f, 0.0f);

    PrimitiveMeshData data{};
    data.material = PrepareMaterial(textureId, material);
    size_t ringVertexCount = 0;
    size_t ringIndexCount = 0;
    if (!CheckedMultiplySize(static_cast<size_t>(divide), 4u,
                             ringVertexCount) ||
        !CheckedMultiplySize(static_cast<size_t>(divide), 6u,
                             ringIndexCount) ||
        !ReserveProceduralMesh(data.vertices, data.indices, ringVertexCount,
                               ringIndexCount)) {
        return std::nullopt;
    }

    for (uint32_t index = 0; index < divide; ++index) {
        const uint32_t base = static_cast<uint32_t>(data.vertices.size());
        const CircleSegment segment = BuildCircleSegment(index, divide);

        data.vertices.push_back({{-segment.sin0 * outerRadius,
                                  segment.cos0 * outerRadius, 0.0f},
                                 {0.0f, 0.0f, 1.0f},
                                 {segment.u0, 0.0f}});
        data.vertices.push_back(
            {{-segment.sin1 * outerRadius, segment.cos1 * outerRadius, 0.0f},
             {0.0f, 0.0f, 1.0f},
             {segment.u1, 0.0f}});
        data.vertices.push_back({{-segment.sin0 * innerRadius,
                                  segment.cos0 * innerRadius, 0.0f},
                                 {0.0f, 0.0f, 1.0f},
                                 {segment.u0, 1.0f}});
        data.vertices.push_back(
            {{-segment.sin1 * innerRadius, segment.cos1 * innerRadius, 0.0f},
             {0.0f, 0.0f, 1.0f},
             {segment.u1, 1.0f}});

        data.indices.push_back(base + 0);
        data.indices.push_back(base + 2);
        data.indices.push_back(base + 1);
        data.indices.push_back(base + 2);
        data.indices.push_back(base + 3);
        data.indices.push_back(base + 1);
    }
    return data;
}

std::optional<PrimitiveMeshData> BuildCylinder(uint32_t textureId,
                                               const Material &material,
                                               uint32_t divide,
                                               float topRadius,
                                               float bottomRadius,
                                               float height) {
    divide = ClampProceduralSegments(divide, 3u, kMaxProceduralSegments);
    topRadius = AtLeastFinite(topRadius, 0.001f, 0.001f);
    bottomRadius = AtLeastFinite(bottomRadius, 0.001f, 0.001f);
    height = AtLeastFinite(height, 0.001f, 0.001f);

    PrimitiveMeshData data{};
    data.material = PrepareMaterial(textureId, material);
    size_t cylinderVertexCount = 0;
    size_t cylinderIndexCount = 0;
    if (!CheckedMultiplySize(static_cast<size_t>(divide), 6u,
                             cylinderVertexCount) ||
        !CheckedMultiplySize(static_cast<size_t>(divide), 6u,
                             cylinderIndexCount) ||
        !ReserveProceduralMesh(data.vertices, data.indices,
                               cylinderVertexCount, cylinderIndexCount)) {
        return std::nullopt;
    }

    for (uint32_t index = 0; index < divide; ++index) {
        const uint32_t base = static_cast<uint32_t>(data.vertices.size());
        const CircleSegment segment = BuildCircleSegment(index, divide);

        data.vertices.push_back({{-segment.sin0 * topRadius, height,
                                  segment.cos0 * topRadius},
                                 {-segment.sin0, 0.0f, segment.cos0},
                                 {segment.u0, 1.0f}});
        data.vertices.push_back(
            {{-segment.sin1 * topRadius, height, segment.cos1 * topRadius},
             {-segment.sin1, 0.0f, segment.cos1},
             {segment.u1, 1.0f}});
        data.vertices.push_back(
            {{-segment.sin0 * bottomRadius, 0.0f,
              segment.cos0 * bottomRadius},
             {-segment.sin0, 0.0f, segment.cos0},
             {segment.u0, 0.0f}});
        data.vertices.push_back(
            {{-segment.sin0 * bottomRadius, 0.0f,
              segment.cos0 * bottomRadius},
             {-segment.sin0, 0.0f, segment.cos0},
             {segment.u0, 0.0f}});
        data.vertices.push_back(
            {{-segment.sin1 * topRadius, height, segment.cos1 * topRadius},
             {-segment.sin1, 0.0f, segment.cos1},
             {segment.u1, 1.0f}});
        data.vertices.push_back(
            {{-segment.sin1 * bottomRadius, 0.0f,
              segment.cos1 * bottomRadius},
             {-segment.sin1, 0.0f, segment.cos1},
             {segment.u1, 0.0f}});

        data.indices.push_back(base + 0);
        data.indices.push_back(base + 1);
        data.indices.push_back(base + 2);
        data.indices.push_back(base + 3);
        data.indices.push_back(base + 4);
        data.indices.push_back(base + 5);
    }
    return data;
}

} // namespace ModelPrimitiveFactory

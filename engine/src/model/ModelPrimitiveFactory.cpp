#include "ModelPrimitiveFactory.h"

#include "core/MathUtils.h"
#include "core/Numeric.h"
#include "model/ModelLimits.h"

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

using namespace DirectX;

namespace ModelPrimitiveFactory {
namespace {
using Numeric::AtLeastFinite;
using Numeric::ClampFinite;

constexpr std::array<Vertex, 4> kPlaneVertices = {{
    {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
}};

constexpr std::array<uint32_t, 6> kPlaneIndices = {0, 1, 2, 2, 1, 3};
constexpr uint32_t kMaxProceduralSegments = 4096;
constexpr uint32_t kMaxTerrainGrid = 1024;
constexpr size_t kMaxProceduralCpuBytes = 128ull * 1024ull * 1024ull;

bool CheckedMultiplySize(size_t lhs, size_t rhs, size_t &out) {
    if (lhs != 0 && rhs > (std::numeric_limits<size_t>::max)() / lhs) {
        return false;
    }
    out = lhs * rhs;
    return true;
}

bool CheckedAddSize(size_t lhs, size_t rhs, size_t &out) {
    if (rhs > (std::numeric_limits<size_t>::max)() - lhs) {
        return false;
    }
    out = lhs + rhs;
    return true;
}

bool CanBuildProceduralMesh(size_t vertexCount, size_t indexCount,
                            size_t extraCpuBytes = 0) {
    if (vertexCount == 0 || indexCount == 0) {
        return false;
    }
    if (vertexCount > ModelLimits::kMaxVerticesPerMesh ||
        indexCount / 3u > ModelLimits::kMaxFacesPerMesh ||
        vertexCount >
            static_cast<size_t>((std::numeric_limits<uint32_t>::max)()) ||
        indexCount >
            static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return false;
    }

    size_t vertexBytes = 0;
    size_t indexBytes = 0;
    size_t totalBytes = 0;
    if (!CheckedMultiplySize(vertexCount, sizeof(Vertex), vertexBytes) ||
        !CheckedMultiplySize(indexCount, sizeof(uint32_t), indexBytes) ||
        !CheckedAddSize(vertexBytes, indexBytes, totalBytes) ||
        !CheckedAddSize(totalBytes, extraCpuBytes, totalBytes)) {
        return false;
    }
    return totalBytes <= kMaxProceduralCpuBytes;
}

bool ReserveProceduralMesh(std::vector<Vertex> &vertices,
                           std::vector<uint32_t> &indices,
                           size_t vertexCount, size_t indexCount,
                           size_t extraCpuBytes = 0) {
    if (!CanBuildProceduralMesh(vertexCount, indexCount, extraCpuBytes)) {
        return false;
    }
    try {
        vertices.reserve(vertexCount);
        indices.reserve(indexCount);
    } catch (...) {
        return false;
    }
    return true;
}

uint32_t ClampProceduralSegments(uint32_t value, uint32_t minimum,
                                 uint32_t maximum) {
    return std::clamp(value, minimum, maximum);
}

Material PrepareMaterial(uint32_t textureId, const Material &material) {
    Material prepared = material;
    if (prepared.baseColorTextureId == UINT32_MAX) {
        prepared.baseColorTextureId = textureId;
    }
    XMStoreFloat4x4(&prepared.uvTransform,
                    XMMatrixTranspose(XMMatrixIdentity()));
    return prepared;
}

float Hash01(int32_t x, int32_t z, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u ^
                 static_cast<uint32_t>(z) * 668265263u ^ seed * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h ^= h >> 16u;
    return static_cast<float>(h & 0x00FFFFFFu) /
           static_cast<float>(0x00FFFFFFu);
}

XMFLOAT3 CalculateFaceNormal(const XMFLOAT3 &a, const XMFLOAT3 &b,
                             const XMFLOAT3 &c) {
    XMVECTOR av = XMLoadFloat3(&a);
    XMVECTOR bv = XMLoadFloat3(&b);
    XMVECTOR cv = XMLoadFloat3(&c);
    XMVECTOR normal = XMVector3Cross(bv - av, cv - av);
    const float lengthSq = XMVectorGetX(XMVector3LengthSq(normal));
    if (!std::isfinite(lengthSq) || lengthSq <= 0.000001f) {
        return {0.0f, 1.0f, 0.0f};
    }
    normal = XMVector3Normalize(normal);
    XMFLOAT3 out{};
    XMStoreFloat3(&out, normal);
    if (!std::isfinite(out.x) || !std::isfinite(out.y) ||
        !std::isfinite(out.z)) {
        return {0.0f, 1.0f, 0.0f};
    }
    if (out.y < 0.0f) {
        out.x = -out.x;
        out.y = -out.y;
        out.z = -out.z;
    }
    return out;
}

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
                                          float width, float height,
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

std::optional<PrimitiveMeshData> BuildSphere(uint32_t textureId,
                                             const Material &material,
                                             uint32_t slice, uint32_t stack,
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

    const float radianPerDivide =
        std::numbers::pi_v<float> * 2.0f / static_cast<float>(divide);
    for (uint32_t index = 0; index < divide; ++index) {
        const uint32_t base = static_cast<uint32_t>(data.vertices.size());
        const float angle = static_cast<float>(index) * radianPerDivide;
        const float angleNext = static_cast<float>(index + 1) * radianPerDivide;
        const float sinV = std::sin(angle);
        const float cosV = std::cos(angle);
        const float sinNext = std::sin(angleNext);
        const float cosNext = std::cos(angleNext);
        const float u = static_cast<float>(index) / static_cast<float>(divide);
        const float uNext =
            static_cast<float>(index + 1) / static_cast<float>(divide);

        data.vertices.push_back({{-sinV * outerRadius, cosV * outerRadius, 0.0f},
                                 {0.0f, 0.0f, 1.0f},
                                 {u, 0.0f}});
        data.vertices.push_back(
            {{-sinNext * outerRadius, cosNext * outerRadius, 0.0f},
             {0.0f, 0.0f, 1.0f},
             {uNext, 0.0f}});
        data.vertices.push_back({{-sinV * innerRadius, cosV * innerRadius, 0.0f},
                                 {0.0f, 0.0f, 1.0f},
                                 {u, 1.0f}});
        data.vertices.push_back(
            {{-sinNext * innerRadius, cosNext * innerRadius, 0.0f},
             {0.0f, 0.0f, 1.0f},
             {uNext, 1.0f}});

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

    const float radianPerDivide =
        std::numbers::pi_v<float> * 2.0f / static_cast<float>(divide);
    for (uint32_t index = 0; index < divide; ++index) {
        const uint32_t base = static_cast<uint32_t>(data.vertices.size());
        const float angle = static_cast<float>(index) * radianPerDivide;
        const float angleNext = static_cast<float>(index + 1) * radianPerDivide;
        const float sinV = std::sin(angle);
        const float cosV = std::cos(angle);
        const float sinNext = std::sin(angleNext);
        const float cosNext = std::cos(angleNext);
        const float u = static_cast<float>(index) / static_cast<float>(divide);
        const float uNext =
            static_cast<float>(index + 1) / static_cast<float>(divide);

        data.vertices.push_back({{-sinV * topRadius, height, cosV * topRadius},
                                 {-sinV, 0.0f, cosV},
                                 {u, 1.0f}});
        data.vertices.push_back(
            {{-sinNext * topRadius, height, cosNext * topRadius},
             {-sinNext, 0.0f, cosNext},
             {uNext, 1.0f}});
        data.vertices.push_back(
            {{-sinV * bottomRadius, 0.0f, cosV * bottomRadius},
             {-sinV, 0.0f, cosV},
             {u, 0.0f}});
        data.vertices.push_back(
            {{-sinV * bottomRadius, 0.0f, cosV * bottomRadius},
             {-sinV, 0.0f, cosV},
             {u, 0.0f}});
        data.vertices.push_back(
            {{-sinNext * topRadius, height, cosNext * topRadius},
             {-sinNext, 0.0f, cosNext},
             {uNext, 1.0f}});
        data.vertices.push_back(
            {{-sinNext * bottomRadius, 0.0f, cosNext * bottomRadius},
             {-sinNext, 0.0f, cosNext},
             {uNext, 0.0f}});

        data.indices.push_back(base + 0);
        data.indices.push_back(base + 1);
        data.indices.push_back(base + 2);
        data.indices.push_back(base + 3);
        data.indices.push_back(base + 4);
        data.indices.push_back(base + 5);
    }
    return data;
}

std::optional<PrimitiveMeshData> BuildLowPolyTerrain(
    uint32_t textureId, const Material &material, uint32_t grid, float size,
    float maxHeight, float flatRadius, uint32_t seed) {
    grid = ClampProceduralSegments(grid, 4u, kMaxTerrainGrid);
    size = AtLeastFinite(size, 1.0f, 1.0f);
    maxHeight = AtLeastFinite(maxHeight, 0.0f, 0.0f);
    flatRadius = ClampFinite(flatRadius, 0.0f, size * 0.499f, 0.0f);

    const float halfSize = size * 0.5f;
    const float step = size / static_cast<float>(grid);
    const uint32_t pointCount = grid + 1u;
    size_t heightCount = 0;
    if (!CheckedMultiplySize(static_cast<size_t>(pointCount),
                             static_cast<size_t>(pointCount), heightCount)) {
        return std::nullopt;
    }

    std::vector<float> heights;
    try {
        heights.resize(heightCount);
    } catch (...) {
        return std::nullopt;
    }

    auto heightAt = [&](uint32_t xIndex, uint32_t zIndex) -> float & {
        return heights[static_cast<size_t>(zIndex) * pointCount + xIndex];
    };

    for (uint32_t z = 0; z < pointCount; ++z) {
        for (uint32_t x = 0; x < pointCount; ++x) {
            const float worldX = -halfSize + static_cast<float>(x) * step;
            const float worldZ = -halfSize + static_cast<float>(z) * step;
            const float dist = std::sqrt(worldX * worldX + worldZ * worldZ);
            const float outerT =
                MathUtils::SmoothStep01((dist - flatRadius) /
                                        (halfSize - flatRadius));
            const float ridge =
                0.45f *
                    Hash01(static_cast<int32_t>(x), static_cast<int32_t>(z),
                           seed) +
                0.35f *
                    Hash01(static_cast<int32_t>(x / 2u),
                           static_cast<int32_t>(z / 2u), seed + 97u) +
                0.20f *
                    Hash01(static_cast<int32_t>(x / 4u),
                           static_cast<int32_t>(z / 4u), seed + 193u);
            const float wave =
                0.5f + 0.5f * std::sinf(worldX * 0.22f + worldZ * 0.17f);
            heightAt(x, z) =
                (-0.28f + maxHeight * (0.35f + ridge * 0.78f + wave * 0.24f)) *
                outerT;
        }
    }

    PrimitiveMeshData data{};
    data.material = PrepareMaterial(textureId, material);
    size_t terrainCellCount = 0;
    size_t terrainVertexCount = 0;
    size_t terrainIndexCount = 0;
    size_t heightBytes = 0;
    if (!CheckedMultiplySize(static_cast<size_t>(grid),
                             static_cast<size_t>(grid), terrainCellCount) ||
        !CheckedMultiplySize(terrainCellCount, 6u, terrainVertexCount) ||
        !CheckedMultiplySize(terrainCellCount, 6u, terrainIndexCount) ||
        !CheckedMultiplySize(heightCount, sizeof(float), heightBytes) ||
        !ReserveProceduralMesh(data.vertices, data.indices, terrainVertexCount,
                               terrainIndexCount, heightBytes)) {
        return std::nullopt;
    }

    auto makePoint = [&](uint32_t xIndex, uint32_t zIndex) {
        const float worldX = -halfSize + static_cast<float>(xIndex) * step;
        const float worldZ = -halfSize + static_cast<float>(zIndex) * step;
        return XMFLOAT3{worldX, heightAt(xIndex, zIndex), worldZ};
    };

    auto pushTriangle = [&](const XMFLOAT3 &a, const XMFLOAT3 &b,
                            const XMFLOAT3 &c) {
        const XMFLOAT3 normal = CalculateFaceNormal(a, b, c);
        const uint32_t base = static_cast<uint32_t>(data.vertices.size());
        data.vertices.push_back({a, normal, {0.0f, 0.0f}});
        data.vertices.push_back({b, normal, {1.0f, 0.0f}});
        data.vertices.push_back({c, normal, {0.5f, 1.0f}});
        data.indices.push_back(base + 0u);
        data.indices.push_back(base + 1u);
        data.indices.push_back(base + 2u);
    };

    for (uint32_t z = 0; z < grid; ++z) {
        for (uint32_t x = 0; x < grid; ++x) {
            XMFLOAT3 p00 = makePoint(x, z);
            XMFLOAT3 p10 = makePoint(x + 1u, z);
            XMFLOAT3 p01 = makePoint(x, z + 1u);
            XMFLOAT3 p11 = makePoint(x + 1u, z + 1u);
            const bool flip =
                Hash01(static_cast<int32_t>(x), static_cast<int32_t>(z),
                       seed + 389u) > 0.5f;
            if (flip) {
                pushTriangle(p00, p10, p11);
                pushTriangle(p00, p11, p01);
            } else {
                pushTriangle(p00, p10, p01);
                pushTriangle(p10, p11, p01);
            }
        }
    }
    return data;
}

} // namespace ModelPrimitiveFactory

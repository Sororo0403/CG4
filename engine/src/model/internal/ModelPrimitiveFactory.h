#pragma once

#include "geometry/ModelVertex.h"
#include "model/Material.h"
#include "model/Vertex.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace ModelPrimitiveFactory {

struct PrimitiveMeshData {
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
    Material material{};
};

std::optional<PrimitiveMeshData> BuildPlane(uint32_t textureId, const Material& material);
std::optional<PrimitiveMeshData> BuildBox(uint32_t textureId, const Material& material, float width,
                                          float height, float depth);
std::optional<PrimitiveMeshData> BuildSphere(uint32_t textureId, const Material& material,
                                             uint32_t slice, uint32_t stack, float radius);
std::optional<PrimitiveMeshData> BuildRing(uint32_t textureId, const Material& material,
                                           uint32_t divide, float outerRadius, float innerRadius);
std::optional<PrimitiveMeshData> BuildCylinder(uint32_t textureId, const Material& material,
                                               uint32_t divide, float topRadius, float bottomRadius,
                                               float height);

} // namespace ModelPrimitiveFactory

#pragma once

#include "ModelPrimitiveFactory.h"

#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>

namespace ModelPrimitiveFactory::Internal {

inline constexpr uint32_t kMaxProceduralSegments = 4096;

bool CheckedMultiplySize(size_t lhs, size_t rhs, size_t& out);
bool CheckedAddSize(size_t lhs, size_t rhs, size_t& out);
bool ReserveProceduralMesh(std::vector<ModelVertex>& vertices, std::vector<uint32_t>& indices,
                           size_t vertexCount, size_t indexCount, size_t extraCpuBytes = 0);
uint32_t ClampProceduralSegments(uint32_t value, uint32_t minimum, uint32_t maximum);
Material PrepareMaterial(uint32_t textureId, const Material& material);

} // namespace ModelPrimitiveFactory::Internal

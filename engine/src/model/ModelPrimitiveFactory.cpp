#include "core/ResourceHandle.h"
#include "internal/ModelPrimitiveFactoryInternal.h"
#include "model/ModelLimits.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <new>

using namespace DirectX;

namespace ModelPrimitiveFactory::Internal {

namespace {

constexpr size_t kMaxProceduralCpuBytes = 128ull * 1024ull * 1024ull;

bool CanBuildProceduralMesh(size_t vertexCount, size_t indexCount, size_t extraCpuBytes = 0) {
    if (vertexCount == 0 || indexCount == 0) {
        return false;
    }
    if (vertexCount > ModelLimits::kMaxVerticesPerMesh ||
        indexCount / 3u > ModelLimits::kMaxFacesPerMesh ||
        vertexCount > static_cast<size_t>((std::numeric_limits<uint32_t>::max)()) ||
        indexCount > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return false;
    }

    size_t vertexBytes = 0;
    size_t indexBytes = 0;
    size_t totalBytes = 0;
    if (!CheckedMultiplySize(vertexCount, sizeof(ModelVertex), vertexBytes) ||
        !CheckedMultiplySize(indexCount, sizeof(uint32_t), indexBytes) ||
        !CheckedAddSize(vertexBytes, indexBytes, totalBytes) ||
        !CheckedAddSize(totalBytes, extraCpuBytes, totalBytes)) {
        return false;
    }
    return totalBytes <= kMaxProceduralCpuBytes;
}

} // namespace

bool CheckedMultiplySize(size_t lhs, size_t rhs, size_t& out) {
    if (lhs != 0 && rhs > (std::numeric_limits<size_t>::max)() / lhs) {
        return false;
    }
    out = lhs * rhs;
    return true;
}

bool CheckedAddSize(size_t lhs, size_t rhs, size_t& out) {
    if (rhs > (std::numeric_limits<size_t>::max)() - lhs) {
        return false;
    }
    out = lhs + rhs;
    return true;
}

bool ReserveProceduralMesh(std::vector<ModelVertex>& vertices, std::vector<uint32_t>& indices,
                           size_t vertexCount, size_t indexCount, size_t extraCpuBytes) {
    if (!CanBuildProceduralMesh(vertexCount, indexCount, extraCpuBytes)) {
        return false;
    }
    try {
        vertices.reserve(vertexCount);
        indices.reserve(indexCount);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

uint32_t ClampProceduralSegments(uint32_t value, uint32_t minimum, uint32_t maximum) {
    return std::clamp(value, minimum, maximum);
}

Material PrepareMaterial(uint32_t textureId, const Material& material) {
    Material prepared = material;
    if (!IsValidResourceId(prepared.baseColorTextureId)) {
        prepared.baseColorTextureId = textureId;
    }
    XMStoreFloat4x4(&prepared.uvTransform, XMMatrixTranspose(XMMatrixIdentity()));
    return prepared;
}

} // namespace ModelPrimitiveFactory::Internal

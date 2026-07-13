#include "graphics/RenderScene.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <new>

namespace {

MaterialFeatureFlags FeaturesFromFlags(RenderObjectFlags flags) {
    MaterialFeatureFlags features = MaterialFeatureFlags::None;
    if (HasRenderObjectFlag(flags, RenderObjectFlags::Transparent)) {
        features |= MaterialFeatureFlags::Transparent;
    }
    if (HasRenderObjectFlag(flags, RenderObjectFlags::CastShadow)) {
        features |= MaterialFeatureFlags::CastShadow;
    }
    if (HasRenderObjectFlag(flags, RenderObjectFlags::ReceiveShadow)) {
        features |= MaterialFeatureFlags::ReceiveShadow;
    }
    if (HasRenderObjectFlag(flags, RenderObjectFlags::MotionVectors)) {
        features |= MaterialFeatureFlags::WritesMotionVectors;
    }
    return features;
}

RenderObjectFlags ApplyMaterialClassification(RenderObjectFlags flags,
                                              const MaterialPipelineKey& key) {
    if (HasMaterialFeature(key.features, MaterialFeatureFlags::Transparent)) {
        flags |= RenderObjectFlags::Transparent;
    } else if (!HasRenderObjectFlag(flags, RenderObjectFlags::Transparent)) {
        flags |= RenderObjectFlags::Opaque;
    }

    if (HasMaterialFeature(key.features, MaterialFeatureFlags::CastShadow)) {
        flags |= RenderObjectFlags::CastShadow;
    }
    if (HasMaterialFeature(key.features, MaterialFeatureFlags::ReceiveShadow)) {
        flags |= RenderObjectFlags::ReceiveShadow;
    }
    if (HasMaterialFeature(key.features, MaterialFeatureFlags::WritesMotionVectors)) {
        flags |= RenderObjectFlags::MotionVectors;
    }

    if (key.domain == MaterialDomain::Foliage) {
        flags |= RenderObjectFlags::Foliage;
    }
    return flags;
}

template <typename T> T NormalizeMeshItem(const T& item) {
    T normalized = item;
    normalized.material = NormalizeMaterialForDraw(normalized.material);
    if (!IsValidResourceId(normalized.textureId)) {
        normalized.textureId = normalized.material.baseColorTextureId;
    }
    if (!IsValidResourceId(normalized.normalTextureId)) {
        normalized.normalTextureId = normalized.material.normalTextureId;
    }
    normalized.materialFeatures |= FeaturesFromFlags(normalized.flags);
    const MaterialPipelineKey key = BuildMaterialPipelineKey(
        normalized.material, normalized.materialDomain, normalized.materialFeatures);
    normalized.materialFeatures = key.features;
    normalized.flags = ApplyMaterialClassification(normalized.flags, key);
    return normalized;
}

} // namespace

void RenderScene::BeginFrame() {
    previousStats_ = stats_;
    meshes_.clear();
    opaqueMeshes_.clear();
    transparentMeshes_.clear();
    shadowMeshes_.clear();
    instancedMeshes_.clear();
    opaqueInstancedMeshes_.clear();
    shadowInstancedMeshes_.clear();
    stats_ = {};
    ReserveForLikelyFrame();
}

void RenderScene::SubmitMesh(const RenderMeshItem& item) {
    RenderMeshItem normalized = Normalize(item);
    if (!IsValid(normalized)) {
        return;
    }
    if (!ReserveForMeshSubmit(normalized)) {
        return;
    }
    const size_t meshSize = meshes_.size();
    const size_t opaqueSize = opaqueMeshes_.size();
    const size_t transparentSize = transparentMeshes_.size();
    const size_t shadowSize = shadowMeshes_.size();
    try {
        meshes_.push_back(normalized);
        CategorizeMesh(normalized);
    } catch (const std::exception&) {
        meshes_.resize(meshSize);
        opaqueMeshes_.resize(opaqueSize);
        transparentMeshes_.resize(transparentSize);
        shadowMeshes_.resize(shadowSize);
        stats_.meshCount = static_cast<uint32_t>(meshes_.size());
        stats_.opaqueMeshCount = static_cast<uint32_t>(opaqueMeshes_.size());
        stats_.transparentMeshCount = static_cast<uint32_t>(transparentMeshes_.size());
        stats_.shadowMeshCount = static_cast<uint32_t>(shadowMeshes_.size());
        return;
    }
    stats_.meshCount = static_cast<uint32_t>(meshes_.size());
}

void RenderScene::SubmitInstancedMesh(const RenderInstancedMeshItem& item) {
    RenderInstancedMeshItem normalized = Normalize(item);
    if (!IsValid(normalized)) {
        return;
    }
    if (!ReserveForInstancedMeshSubmit(normalized)) {
        return;
    }
    const size_t instancedSize = instancedMeshes_.size();
    const size_t opaqueInstancedSize = opaqueInstancedMeshes_.size();
    const size_t shadowInstancedSize = shadowInstancedMeshes_.size();
    try {
        instancedMeshes_.push_back(normalized);
        CategorizeInstancedMesh(normalized);
    } catch (const std::exception&) {
        instancedMeshes_.resize(instancedSize);
        opaqueInstancedMeshes_.resize(opaqueInstancedSize);
        shadowInstancedMeshes_.resize(shadowInstancedSize);
        stats_.instancedMeshCount = static_cast<uint32_t>(instancedMeshes_.size());
        stats_.opaqueInstancedMeshCount = static_cast<uint32_t>(opaqueInstancedMeshes_.size());
        stats_.shadowInstancedMeshCount = static_cast<uint32_t>(shadowInstancedMeshes_.size());
        return;
    }
    stats_.instancedMeshCount = static_cast<uint32_t>(instancedMeshes_.size());
}

std::span<const RenderMeshItem> RenderScene::Meshes() const {
    return meshes_;
}

std::span<const RenderMeshItem> RenderScene::OpaqueMeshes() const {
    return opaqueMeshes_;
}

std::span<const RenderMeshItem> RenderScene::TransparentMeshes() const {
    return transparentMeshes_;
}

std::span<const RenderMeshItem> RenderScene::ShadowMeshes() const {
    return shadowMeshes_;
}

std::span<const RenderInstancedMeshItem> RenderScene::InstancedMeshes() const {
    return instancedMeshes_;
}

std::span<const RenderInstancedMeshItem> RenderScene::OpaqueInstancedMeshes() const {
    return opaqueInstancedMeshes_;
}

std::span<const RenderInstancedMeshItem> RenderScene::ShadowInstancedMeshes() const {
    return shadowInstancedMeshes_;
}

bool RenderScene::ReserveForMeshSubmit(const RenderMeshItem& item) {
    if (meshes_.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return false;
    }
    try {
        meshes_.reserve(meshes_.size() + 1u);
        if (HasRenderObjectFlag(item.flags, RenderObjectFlags::Transparent)) {
            transparentMeshes_.reserve(transparentMeshes_.size() + 1u);
        } else if (HasRenderObjectFlag(item.flags, RenderObjectFlags::Opaque)) {
            opaqueMeshes_.reserve(opaqueMeshes_.size() + 1u);
        }
        if (HasRenderObjectFlag(item.flags, RenderObjectFlags::CastShadow)) {
            shadowMeshes_.reserve(shadowMeshes_.size() + 1u);
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool RenderScene::ReserveForInstancedMeshSubmit(const RenderInstancedMeshItem& item) {
    if (instancedMeshes_.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return false;
    }
    try {
        instancedMeshes_.reserve(instancedMeshes_.size() + 1u);
        if (HasRenderObjectFlag(item.flags, RenderObjectFlags::Opaque) &&
            !HasRenderObjectFlag(item.flags, RenderObjectFlags::Transparent)) {
            opaqueInstancedMeshes_.reserve(opaqueInstancedMeshes_.size() + 1u);
        }
        if (HasRenderObjectFlag(item.flags, RenderObjectFlags::CastShadow)) {
            shadowInstancedMeshes_.reserve(shadowInstancedMeshes_.size() + 1u);
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

void RenderScene::CategorizeMesh(const RenderMeshItem& item) {
    if (HasRenderObjectFlag(item.flags, RenderObjectFlags::Transparent)) {
        transparentMeshes_.push_back(item);
        stats_.transparentMeshCount = static_cast<uint32_t>(transparentMeshes_.size());
    } else if (HasRenderObjectFlag(item.flags, RenderObjectFlags::Opaque)) {
        opaqueMeshes_.push_back(item);
        stats_.opaqueMeshCount = static_cast<uint32_t>(opaqueMeshes_.size());
    }

    if (HasRenderObjectFlag(item.flags, RenderObjectFlags::CastShadow)) {
        shadowMeshes_.push_back(item);
        stats_.shadowMeshCount = static_cast<uint32_t>(shadowMeshes_.size());
    }
}

void RenderScene::CategorizeInstancedMesh(const RenderInstancedMeshItem& item) {
    if (HasRenderObjectFlag(item.flags, RenderObjectFlags::Opaque) &&
        !HasRenderObjectFlag(item.flags, RenderObjectFlags::Transparent)) {
        opaqueInstancedMeshes_.push_back(item);
        stats_.opaqueInstancedMeshCount = static_cast<uint32_t>(opaqueInstancedMeshes_.size());
    }

    if (HasRenderObjectFlag(item.flags, RenderObjectFlags::CastShadow)) {
        shadowInstancedMeshes_.push_back(item);
        stats_.shadowInstancedMeshCount = static_cast<uint32_t>(shadowInstancedMeshes_.size());
    }
}

RenderMeshItem RenderScene::Normalize(const RenderMeshItem& item) {
    return NormalizeMeshItem(item);
}

RenderInstancedMeshItem RenderScene::Normalize(const RenderInstancedMeshItem& item) {
    return NormalizeMeshItem(item);
}

bool RenderScene::IsValid(const RenderMeshItem& item) {
    return item.mesh != nullptr;
}

bool RenderScene::IsValid(const RenderInstancedMeshItem& item) {
    return item.mesh != nullptr && item.instances != nullptr && item.instanceCount > 0u;
}

void RenderScene::ReserveForLikelyFrame() {
    try {
        meshes_.reserve(previousStats_.meshCount);
        opaqueMeshes_.reserve(previousStats_.opaqueMeshCount);
        transparentMeshes_.reserve(previousStats_.transparentMeshCount);
        shadowMeshes_.reserve(previousStats_.shadowMeshCount);
        instancedMeshes_.reserve(previousStats_.instancedMeshCount);
        opaqueInstancedMeshes_.reserve(previousStats_.opaqueInstancedMeshCount);
        shadowInstancedMeshes_.reserve(previousStats_.shadowInstancedMeshCount);
    } catch (const std::exception&) {
    }
}

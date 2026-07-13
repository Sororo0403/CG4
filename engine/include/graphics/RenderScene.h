#pragma once

#include "core/ResourceHandle.h"
#include "model/InstanceData.h"
#include "model/Material.h"
#include "model/MaterialFeatures.h"
#include "model/MeshGpuCullBuffer.h"
#include "model/Transform.h"

#include <DirectXMath.h>
#include <cstdint>
#include <span>
#include <vector>

struct Mesh;

enum class RenderObjectFlags : uint32_t {
    None = 0,
    Opaque = 1u << 0,
    Transparent = 1u << 1,
    CastShadow = 1u << 2,
    ReceiveShadow = 1u << 3,
    MotionVectors = 1u << 4,
    GpuCull = 1u << 5,
    Foliage = 1u << 6,
};

inline constexpr RenderObjectFlags operator|(RenderObjectFlags lhs, RenderObjectFlags rhs) {
    return static_cast<RenderObjectFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

inline constexpr RenderObjectFlags operator&(RenderObjectFlags lhs, RenderObjectFlags rhs) {
    return static_cast<RenderObjectFlags>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

inline RenderObjectFlags& operator|=(RenderObjectFlags& lhs, RenderObjectFlags rhs) {
    lhs = lhs | rhs;
    return lhs;
}

inline constexpr bool HasRenderObjectFlag(RenderObjectFlags flags, RenderObjectFlags flag) {
    return static_cast<uint32_t>(flags & flag) != 0u;
}

struct RenderMeshItem {
    const Mesh* mesh = nullptr;
    Material material{};
    Transform transform{};
    uint32_t textureId = kInvalidResourceId;
    uint32_t normalTextureId = kInvalidResourceId;
    uint32_t pipelineId = kInvalidResourceId;
    MaterialDomain materialDomain = MaterialDomain::Surface;
    MaterialFeatureFlags materialFeatures = MaterialFeatureFlags::None;
    RenderObjectFlags flags = RenderObjectFlags::Opaque | RenderObjectFlags::CastShadow |
                              RenderObjectFlags::ReceiveShadow;
    uint32_t objectId = kInvalidResourceId;
};

struct RenderInstancedMeshItem {
    const Mesh* mesh = nullptr;
    Material material{};
    const InstanceData* instances = nullptr;
    uint32_t instanceCount = 0;
    uint32_t textureId = kInvalidResourceId;
    uint32_t normalTextureId = kInvalidResourceId;
    uint32_t pipelineId = kInvalidResourceId;
    MaterialDomain materialDomain = MaterialDomain::Surface;
    MaterialFeatureFlags materialFeatures = MaterialFeatureFlags::None;
    RenderObjectFlags flags = RenderObjectFlags::Opaque | RenderObjectFlags::CastShadow |
                              RenderObjectFlags::ReceiveShadow | RenderObjectFlags::GpuCull;
    MeshGpuCullBounds localBounds{};
    uint32_t objectIdBase = kInvalidResourceId;
};

struct RenderSceneStats {
    uint32_t meshCount = 0;
    uint32_t instancedMeshCount = 0;
    uint32_t opaqueMeshCount = 0;
    uint32_t transparentMeshCount = 0;
    uint32_t shadowMeshCount = 0;
    uint32_t opaqueInstancedMeshCount = 0;
    uint32_t shadowInstancedMeshCount = 0;
};

class RenderScene {
public:
    void BeginFrame();

    void SubmitMesh(const RenderMeshItem& item);
    void SubmitInstancedMesh(const RenderInstancedMeshItem& item);

    std::span<const RenderMeshItem> Meshes() const;
    std::span<const RenderMeshItem> OpaqueMeshes() const;
    std::span<const RenderMeshItem> TransparentMeshes() const;
    std::span<const RenderMeshItem> ShadowMeshes() const;

    std::span<const RenderInstancedMeshItem> InstancedMeshes() const;
    std::span<const RenderInstancedMeshItem> OpaqueInstancedMeshes() const;
    std::span<const RenderInstancedMeshItem> ShadowInstancedMeshes() const;

    const RenderSceneStats& GetStats() const {
        return stats_;
    }
    bool Empty() const {
        return meshes_.empty() && instancedMeshes_.empty();
    }

private:
    bool ReserveForMeshSubmit(const RenderMeshItem& item);
    bool ReserveForInstancedMeshSubmit(const RenderInstancedMeshItem& item);
    void CategorizeMesh(const RenderMeshItem& item);
    void CategorizeInstancedMesh(const RenderInstancedMeshItem& item);
    static RenderMeshItem Normalize(const RenderMeshItem& item);
    static RenderInstancedMeshItem Normalize(const RenderInstancedMeshItem& item);
    static bool IsValid(const RenderMeshItem& item);
    static bool IsValid(const RenderInstancedMeshItem& item);
    void ReserveForLikelyFrame();

    std::vector<RenderMeshItem> meshes_;
    std::vector<RenderMeshItem> opaqueMeshes_;
    std::vector<RenderMeshItem> transparentMeshes_;
    std::vector<RenderMeshItem> shadowMeshes_;

    std::vector<RenderInstancedMeshItem> instancedMeshes_;
    std::vector<RenderInstancedMeshItem> opaqueInstancedMeshes_;
    std::vector<RenderInstancedMeshItem> shadowInstancedMeshes_;

    RenderSceneStats stats_{};
    RenderSceneStats previousStats_{};
};

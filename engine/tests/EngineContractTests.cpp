#include "EngineTestSupport.h"
#include "EngineTestSuites.h"

#include "core/ResourceHandle.h"
#include "input/InputReplayLimits.h"
#include "model/MaterialManager.h"
#include "model/MeshManager.h"
#include "model/ModelManager.h"
#include "model/ModelLimits.h"
#include "model/RendererSceneConstants.h"
#include "sound/AudioLimits.h"
#include "sound/SoundManager.h"
#include "texture/TextureManager.h"
#include "texture/TextureLimits.h"
#include "../src/model/internal/RendererPipelineVariantUtils.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

namespace EngineTests {

void TestResourceLimitContracts() {
    Expect(InputReplayLimits::kMaxFrames == 1000000,
           "input replay frame limit changed unexpectedly");
    Expect(InputReplayLimits::kMaxFileBytes == 128ull * 1024ull * 1024ull,
           "input replay file byte limit changed unexpectedly");
    Expect(AudioLimits::kMaxDecodedPcmBytes == 256u * 1024u * 1024u,
           "decoded PCM byte limit changed unexpectedly");
    Expect(TextureLimits::kMaxFileBytes == 512ull * 1024ull * 1024ull,
           "texture file byte limit changed unexpectedly");
    Expect(TextureLimits::kMaxMemoryBytes == 512ull * 1024ull * 1024ull,
           "texture memory byte limit changed unexpectedly");
    Expect(TextureLimits::kMaxDimension == 16384u,
           "texture dimension limit changed unexpectedly");
    Expect(ModelLimits::kMaxFileBytes == 512ull * 1024ull * 1024ull,
           "model file byte limit changed unexpectedly");
    Expect(ModelLimits::kMaxVerticesPerMesh == 1048576u,
           "model vertex limit changed unexpectedly");
}

void TestResourceHandleContracts() {
    static_assert(std::is_same_v<
                  decltype(std::declval<MeshManager &>().CreateMeshHandle(
                      nullptr, 0u, 0u, nullptr, 0u)),
                  MeshHandle>);
    static_assert(std::is_same_v<
                  decltype(std::declval<TextureManager &>().LoadHandle(
                      std::declval<const std::wstring &>())),
                  TextureHandle>);
    static_assert(std::is_same_v<
                  decltype(std::declval<TextureManager &>()
                               .GetWhiteTextureHandle()),
                  TextureHandle>);
    static_assert(std::is_same_v<
                  decltype(std::declval<MaterialManager &>()
                               .CreateMaterialHandle(
                                   std::declval<const Material &>())),
                  MaterialHandle>);
    static_assert(std::is_same_v<
                  decltype(std::declval<ModelManager &>().LoadHandle(
                      std::declval<const std::wstring &>())),
                  ModelHandle>);
    static_assert(std::is_same_v<
                  decltype(std::declval<ModelManager &>().CreateBoxHandle(
                      TextureHandle(), std::declval<const Material &>())),
                  ModelHandle>);
    static_assert(std::is_same_v<
                  decltype(std::declval<SoundManager &>().LoadHandle(
                      std::declval<const std::wstring &>())),
                  SoundHandle>);
    static_assert(std::is_same_v<
                  decltype(std::declval<SoundManager &>().Play(SoundHandle())),
                  VoiceHandle>);

    const MeshHandle invalidMesh{};
    Expect(!invalidMesh.IsValid(), "default mesh handle must be invalid");
    Expect(!invalidMesh, "default mesh handle bool conversion must be false");
    Expect(invalidMesh.Get() == kInvalidResourceId,
           "default handle must use the shared invalid resource id");
    Expect(!IsValidResourceId(invalidMesh.Get()),
           "invalid resource id helper must reject default handles");

    const MeshHandle mesh = MakeResourceHandle<MeshHandleTag>(42u);
    const MeshHandle sameMesh(42u);
    const MeshHandle otherMesh(7u);
    Expect(mesh.IsValid(), "explicit mesh handle id must be valid");
    Expect(static_cast<bool>(mesh), "valid mesh handle bool conversion failed");
    Expect(ToResourceId(mesh) == 42u, "handle id extraction changed");
    Expect(IsValidResourceId(mesh.Get()),
           "valid resource id helper must accept normal ids");
    Expect(mesh == sameMesh, "matching handles must compare equal");
    Expect(mesh != otherMesh, "different handles must compare unequal");
}

void TestMeshPipelineVariantContracts() {
    using RendererPipelineVariantUtils::MaterialPipelineVariantIndex;
    using RendererPipelineVariantUtils::PipelineBlendMode;
    using RendererPipelineVariantUtils::PipelineVariantIndex;
    using RendererPipelineVariantUtils::ToD3D12CullMode;

    static_assert(kMeshPipelineVariantCount == 12,
                  "mesh material-driven pipeline variant count changed");
    Expect(MaterialPipelineVariantIndex(false, MaterialCullMode::None, false) == 0,
           "opaque/no-cull/test-only variant index changed");
    Expect(MaterialPipelineVariantIndex(false, MaterialCullMode::None, true) == 1,
           "opaque/no-cull/test-write variant index changed");
    Expect(MaterialPipelineVariantIndex(false, MaterialCullMode::Front, false) == 2,
           "opaque/front-cull/test-only variant index changed");
    Expect(MaterialPipelineVariantIndex(false, MaterialCullMode::Back, true) == 5,
           "opaque/back-cull/test-write variant index changed");
    Expect(MaterialPipelineVariantIndex(true, MaterialCullMode::None, false) == 6,
           "transparent/no-cull/test-only variant index changed");
    Expect(MaterialPipelineVariantIndex(true, MaterialCullMode::Back, true) == 11,
           "transparent/back-cull/test-write variant index changed");
    Expect(PipelineVariantIndex(PipelineBlendMode::Additive,
                                MaterialCullMode::Back, true) == 17,
           "additive/back-cull/test-write variant index changed");

    Expect(ToD3D12CullMode(MaterialCullMode::None) == D3D12_CULL_MODE_NONE,
           "material no-cull mapping changed");
    Expect(ToD3D12CullMode(MaterialCullMode::Front) == D3D12_CULL_MODE_FRONT,
           "material front-cull mapping changed");
    Expect(ToD3D12CullMode(MaterialCullMode::Back) == D3D12_CULL_MODE_BACK,
           "material back-cull mapping changed");
    Expect(ToD3D12CullMode(MeshCullMode::None) == D3D12_CULL_MODE_NONE,
           "mesh no-cull mapping changed");
    Expect(ToD3D12CullMode(MeshCullMode::Front) == D3D12_CULL_MODE_FRONT,
           "mesh front-cull mapping changed");
    Expect(ToD3D12CullMode(MeshCullMode::Back) == D3D12_CULL_MODE_BACK,
           "mesh back-cull mapping changed");
}

void TestMaterialPbrTexturePackingContracts() {
    Material material{};
    material.roughnessTextureId = 10u;
    material.metallicTextureId = 10u;

    Material normalized = NormalizeMaterialForDraw(material);
    Expect(normalized.pbrTextureParams.x == 1.0f,
           "roughness texture flag must be set when roughness id is valid");
    Expect(normalized.pbrTextureParams.y == 1.0f,
           "metallic texture flag must be set when metallic id is valid");
    Expect(normalized.pbrTextureParams.z == 0.0f,
           "shared PBR texture must not imply ORM packing");
    Expect(normalized.pbrTextureParams.w == 0.0f,
           "shared PBR texture must not imply metallic-roughness packing");

    material.pbrTexturePacking =
        static_cast<int32_t>(PbrTexturePacking::OcclusionRoughnessMetallic);
    normalized = NormalizeMaterialForDraw(material);
    Expect(normalized.pbrTextureParams.z == 1.0f,
           "ORM packing must set the ORM shader flag");
    Expect(normalized.pbrTextureParams.w == 0.0f,
           "ORM packing must not set the metallic-roughness shader flag");

    material.pbrTexturePacking =
        static_cast<int32_t>(PbrTexturePacking::MetallicRoughness);
    normalized = NormalizeMaterialForDraw(material);
    Expect(normalized.pbrTextureParams.z == 0.0f,
           "metallic-roughness packing must not set the ORM shader flag");
    Expect(normalized.pbrTextureParams.w == 1.0f,
           "metallic-roughness packing must set the packed MR shader flag");

    material.metallicTextureId = 11u;
    normalized = NormalizeMaterialForDraw(material);
    Expect(normalized.pbrTextureParams.z == 0.0f &&
               normalized.pbrTextureParams.w == 0.0f,
           "separate PBR textures must ignore packed texture flags");

    material.pbrTexturePacking = 999;
    normalized = NormalizeMaterialForDraw(material);
    Expect(normalized.pbrTexturePacking ==
               static_cast<int32_t>(PbrTexturePacking::Separate),
           "invalid PBR packing values must normalize to Separate");
}

void TestRendererSceneConstantLayout() {
    Expect(sizeof(RendererPointLightConstant) == 32,
           "point light constant must remain two float4 values");
    Expect(sizeof(RendererSpotLightConstant) == 64,
           "spot light constant must remain four float4 values");
    Expect(sizeof(MeshSceneConstBufferData) % 16 == 0,
           "mesh scene constants must stay 16-byte sized");
    Expect(sizeof(ModelSceneConstBufferData) % 16 == 0,
           "model scene constants must stay 16-byte sized");
    Expect(offsetof(MeshSceneConstBufferData, viewProjection) % 16 == 0,
           "mesh view-projection matrix must start on a float4 boundary");
    Expect(offsetof(ModelSceneConstBufferData, viewProjection) % 16 == 0,
           "model view-projection matrix must start on a float4 boundary");
}

} // namespace EngineTests

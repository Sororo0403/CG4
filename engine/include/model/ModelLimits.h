#pragma once

#include <cstddef>
#include <cstdint>

namespace ModelLimits {

inline constexpr std::uintmax_t kMaxFileBytes = 512ull * 1024ull * 1024ull;
inline constexpr std::size_t kMaxMeshes = 4096u;
inline constexpr std::size_t kMaxMaterials = 4096u;
inline constexpr std::size_t kMaxEmbeddedTextures = 1024u;
inline constexpr std::size_t kMaxAnimations = 256u;
inline constexpr std::size_t kMaxAnimationChannels = 8192u;
inline constexpr std::size_t kMaxAnimationKeysPerChannel = 262144u;
inline constexpr std::size_t kMaxAnimationKeysTotal = 1048576u;
inline constexpr std::size_t kMaxVerticesPerMesh = 1048576u;
inline constexpr std::size_t kMaxFacesPerMesh = 2097152u;
inline constexpr std::size_t kMaxBonesPerMesh = 1024u;
inline constexpr std::size_t kMaxTotalVertices = 4194304u;
inline constexpr std::size_t kMaxTotalFaces = 8388608u;
inline constexpr std::size_t kMaxTotalBones = 4096u;
inline constexpr std::size_t kMaxEmbeddedTextureBytes = 512ull * 1024ull * 1024ull;
inline constexpr std::size_t kMaxEmbeddedTexturePixels = 268435456u;

} // namespace ModelLimits

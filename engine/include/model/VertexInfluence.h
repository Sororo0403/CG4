#pragma once

#include <array>
#include <cstdint>

constexpr uint32_t kNumMaxInfluence = 4;

/// <summary>
/// GPUへ渡す頂点スキニング情報
/// </summary>
struct VertexInfluence {
    std::array<float, kNumMaxInfluence> weights{};
    std::array<int32_t, kNumMaxInfluence> jointIndices{};
};

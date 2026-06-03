#pragma once

#include <cstddef>
#include <cstdint>

namespace InputReplayLimits {
inline constexpr std::size_t kMaxFrames = 1000000;
inline constexpr std::uintmax_t kMaxFileBytes = 128ull * 1024ull * 1024ull;
} // namespace InputReplayLimits

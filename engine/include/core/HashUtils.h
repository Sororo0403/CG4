#pragma once

#include <cstdint>

namespace HashUtils {

inline constexpr uint32_t kGoldenRatio32 = 0x9E3779B9u;

constexpr uint32_t Mix32(uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7FEB352Du;
    value ^= value >> 15u;
    value *= 0x846CA68Bu;
    value ^= value >> 16u;
    return value;
}

constexpr uint32_t MixMurmur32(uint32_t value) {
    value ^= value >> 16u;
    value *= 2246822519u;
    value ^= value >> 13u;
    value *= 3266489917u;
    value ^= value >> 16u;
    return value;
}

constexpr uint32_t Combine(uint32_t value, uint32_t salt) {
    return Mix32(value ^ (salt * kGoldenRatio32));
}

constexpr float UnitFloat24(uint32_t hash) {
    return static_cast<float>(hash & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

constexpr float UnitFloat24Inclusive(uint32_t hash) {
    return static_cast<float>(hash & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
}

constexpr float UnitFloat01(uint32_t value) {
    return UnitFloat24(Mix32(value));
}

constexpr float UnitFloat01Inclusive(uint32_t value) {
    return UnitFloat24Inclusive(Mix32(value));
}

constexpr float UnitFloat01MurmurInclusive(uint32_t value) {
    return UnitFloat24Inclusive(MixMurmur32(value));
}

constexpr float UnitFloat01(uint32_t value, uint32_t salt) {
    return UnitFloat24(Combine(value, salt));
}

constexpr float SignedFloat11(uint32_t value) {
    return UnitFloat01(value) * 2.0f - 1.0f;
}

constexpr float SignedFloat11(uint32_t value, uint32_t salt) {
    return UnitFloat01(value, salt) * 2.0f - 1.0f;
}

} // namespace HashUtils

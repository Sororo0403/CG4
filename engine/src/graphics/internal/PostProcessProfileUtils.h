#pragma once

#include "graphics/PostProcessSystem.h"

namespace PostProcessProfileUtils {

inline bool HasSpecial(const PostProcessProfile& profile) {
    return profile.special.mode == PostProcessSpecialMode::Vignette ||
           profile.special.mode == PostProcessSpecialMode::Dissolve;
}

inline bool HasVignette(const PostProcessProfile& profile) {
    return (profile.vignette.enabled && profile.vignette.strength > 0.0f) ||
           profile.vignette.primaryTintStrength > 0.0f ||
           profile.vignette.secondaryTintStrength > 0.0f;
}

inline bool HasRandomNoise(const PostProcessProfile& profile) {
    return profile.randomNoise.mode != PostProcessRandomMode::None &&
           profile.randomNoise.strength > 0.0f;
}

inline bool HasToon(const PostProcessProfile& profile) {
    return profile.toon.enabled && profile.toon.strength > 0.0f;
}

inline void EnsureFinalToneMapEnabled(PostProcessProfile& profile) {
    if (profile.tonemap.enabled) {
        return;
    }

    const PostProcessTonemapSettings defaults{};
    profile.tonemap.enabled = true;
    profile.tonemap.exposure = defaults.exposure;
    profile.tonemap.gamma = defaults.gamma;
}

} // namespace PostProcessProfileUtils

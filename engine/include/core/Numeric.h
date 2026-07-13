#pragma once

#include <algorithm>
#include <cmath>

namespace Numeric {

inline float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

inline float ClampFinite(float value, float minimum, float maximum, float fallback) {
    return std::clamp(FiniteOr(value, fallback), minimum, maximum);
}

inline float AtLeastFinite(float value, float minimum, float fallback) {
    return (std::max)(FiniteOr(value, fallback), minimum);
}

} // namespace Numeric

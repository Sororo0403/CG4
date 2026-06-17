#pragma once

#include <cstddef>
#include <limits>

namespace CoreAlignment {

inline size_t AlignUp(size_t value, size_t alignment) noexcept {
    if (alignment <= 1) {
        return value;
    }
    const size_t addend = alignment - 1;
    if (value > (std::numeric_limits<size_t>::max)() - addend) {
        return (std::numeric_limits<size_t>::max)();
    }
    return ((value + addend) / alignment) * alignment;
}

} // namespace CoreAlignment

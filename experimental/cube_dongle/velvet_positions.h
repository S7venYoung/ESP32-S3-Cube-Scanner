#pragma once
#include "split_state.h"

namespace cube_dongle {
// Verified against velvet.dtsi global matrix-transform: 61 positions, not 46.
constexpr SplitState::Ownership VelvetOwnership() {
    SplitState::Ownership result{};
    for (size_t p = 0; p < 61; ++p) {
        const bool left = p < 6 || (p >= 12 && p <= 18) || (p >= 25 && p <= 31) ||
                          (p >= 38 && p <= 44) || (p >= 51 && p <= 55);
        result[left ? 0 : 1][p / 8] |= static_cast<uint8_t>(1u << (p % 8));
    }
    return result;
}
}  // namespace cube_dongle

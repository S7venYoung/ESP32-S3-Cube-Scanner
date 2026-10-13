#pragma once
#include <array>
#include <cstdint>
#include "split_state.h"

namespace cube_dongle {
// Host-thread owned, bounded five-second typing window. Initial held keys are
// primed, not counted as typing; repeated snapshots and modifiers add no WPM.
class TypingActivity {
public:
    template <size_t Positions, size_t Layers>
    void Observe(const SplitState::Snapshot& snapshot,
                 const KeyboardState<Positions, Layers>& keyboard, int64_t now_ms) {
        if (!primed_) {
            previous_ = snapshot;
            start_ms_ = now_ms;
            primed_ = true;
            return;
        }
        const int64_t second = now_ms / 1000;
        auto& bucket = buckets_[second % buckets_.size()];
        if (bucket.second != second)
            bucket = {second, 0};
        for (size_t p = 0; p < Positions; ++p) {
            const uint8_t mask = 1u << (p % 8);
            const auto binding = keyboard.PressedBinding(p);
            if ((snapshot[p / 8] & mask) && !(previous_[p / 8] & mask) &&
                binding.kind == Kind::Key && binding.value >= 4 && binding.value < 0xe0) {
                ++bucket.count;
                last_press_ms_ = now_ms;
            }
        }
        previous_ = snapshot;
    }
    int Wpm(int64_t now_ms) const {
        if (last_press_ms_ < 0 || now_ms - last_press_ms_ >= 750)
            return 0;
        unsigned count = 0;
        const int64_t second = now_ms / 1000;
        for (const auto& b : buckets_)
            if (b.second >= 0 && second - b.second >= 0 && second - b.second < 5)
                count += b.count;
        const int64_t elapsed = now_ms - start_ms_;
        const int64_t window = elapsed < 1000 ? 1000 : elapsed > 5000 ? 5000 : elapsed;
        const auto rate = count * 12000ull / window;  // five key presses per word
        return rate > 999 ? 999 : static_cast<int>(rate);
    }
    void Reset() { *this = TypingActivity{}; }

private:
    struct Bucket {
        int64_t second = -1;
        unsigned count = 0;
    };
    std::array<Bucket, 5> buckets_{};
    SplitState::Snapshot previous_{};
    int64_t start_ms_ = 0, last_press_ms_ = -1;
    bool primed_ = false;
};
inline uint8_t UiModifiers(uint8_t hid) {
    return ((hid & 0x11) ? 1 : 0) | ((hid & 0x44) ? 2 : 0) | ((hid & 0x88) ? 4 : 0) |
           ((hid & 0x22) ? 8 : 0);
}
}  // namespace cube_dongle

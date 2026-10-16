#pragma once
#include <cstdint>

// Two protocols carry the same HID state. A repeated old value from one
// source must not overwrite a newer transition observed in the other.
class ModifierTimeline {
public:
    bool Observe(unsigned source, uint8_t value, int64_t now) {
        if (source > 1)
            return false;
        auto& s = sources_[source];
        const bool transition = s.known && s.value != value;
        const bool initial = !s.known && (seen_ < 0 || value != 0);
        s.known = true;
        s.value = value;
        const auto before = Get(now);
        if (transition || initial) {
            value_ = value;
            seen_ = now;
        } else if (value == value_ && seen_ >= 0 && (source == 0 || now - seen_ <= kLeaseUs)) {
            // Source zero has passed sequence validation: this is a new sample.
            seen_ = now;
        }
        return before != Get(now);
    }
    uint8_t Get(int64_t now) const {
        return seen_ >= 0 && now >= seen_ && now - seen_ <= kLeaseUs ? value_ : 0;
    }

private:
    static constexpr int64_t kLeaseUs = 3000000;
    struct Source {
        bool known = false;
        uint8_t value = 0;
    } sources_[2];
    uint8_t value_ = 0;
    int64_t seen_ = -1;
};
uint8_t GetScannerModifiers();

#pragma once
#include "split_state.h"

namespace cube_dongle {
struct ReportFrame {
    KeyboardReport keyboard{};
    uint8_t media = 0;
    bool operator==(const ReportFrame& other) const {
        return keyboard == other.keyboard && media == other.media;
    }
    bool Neutral() const { return *this == ReportFrame{}; }
};
// External synchronization required. Preserve taps as ordered transitions.
// On overflow/disconnect, release everything and require physical neutral input.
template <size_t Capacity>
class ReportQueue {
public:
    static_assert(Capacity >= 2);
    bool Push(const ReportFrame& frame) {
        if (inhibited_) {
            if (!frame.Neutral())
                return false;
            inhibited_ = false;
        }
        if (frame == latest_)
            return true;
        if (count_ == Capacity) {
            ReleaseAndInhibit();
            return false;
        }
        frames_[(head_ + count_) % Capacity] = frame;
        ++count_;
        latest_ = frame;
        return true;
    }
    void ReleaseAndInhibit() {
        head_ = 0;
        count_ = 1;
        frames_[0] = {};
        latest_ = {};
        inhibited_ = true;
    }
    const ReportFrame* Front() const { return count_ ? &frames_[head_] : nullptr; }
    void Pop() {
        if (count_) {
            head_ = (head_ + 1) % Capacity;
            --count_;
        }
    }
    ReportFrame Latest() const { return latest_; }
    bool Inhibited() const { return inhibited_; }

private:
    std::array<ReportFrame, Capacity> frames_{};
    size_t head_ = 0, count_ = 0;
    ReportFrame latest_{};
    bool inhibited_ = false;
};
}  // namespace cube_dongle

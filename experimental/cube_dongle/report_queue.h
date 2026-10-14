#pragma once
#include "optional_input.h"
#include "split_state.h"

namespace cube_dongle {
struct ReportFrame {
    KeyboardReport keyboard{};
    uint8_t media = 0;
    PointerReport pointer{};
    bool Motion() const { return pointer.x || pointer.y || pointer.wheel || pointer.pan; }
    bool operator==(const ReportFrame& other) const {
        return keyboard == other.keyboard && media == other.media &&
               PointerBytes(pointer) == PointerBytes(other.pointer);
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
        const auto same_state = [](const ReportFrame& a, const ReportFrame& b) {
            return a.keyboard == b.keyboard && a.media == b.media &&
                   a.pointer.buttons == b.pointer.buttons;
        };
        // A zero delta is not a mouse release. Suppress it when held state did
        // not change, rather than spending three USB/BLE transfers per tick.
        if (!frame.Motion() && same_state(frame, latest_))
            return true;
        if (frame.Motion() && count_ > 1) {
            auto& tail = frames_[(head_ + count_ - 1) % Capacity];
            const int x = int(tail.pointer.x) + frame.pointer.x,
                      y = int(tail.pointer.y) + frame.pointer.y;
            const int wheel = int(tail.pointer.wheel) + frame.pointer.wheel,
                      pan = int(tail.pointer.pan) + frame.pointer.pan;
            // Never mutate Front(): its pointer report may already be in flight.
            // Combine only additive deltas with identical key/button state.
            if (tail.Motion() && same_state(tail, frame) && x >= -32768 && x <= 32767 &&
                y >= -32768 && y <= 32767 && wheel >= -128 && wheel <= 127 && pan >= -128 &&
                pan <= 127) {
                tail.pointer.x = x;
                tail.pointer.y = y;
                tail.pointer.wheel = wheel;
                tail.pointer.pan = pan;
                latest_ = tail;
                return true;
            }
        }
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

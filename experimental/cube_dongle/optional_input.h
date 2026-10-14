#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>

namespace cube_dongle {
// Relative motion must not be replayed across a host/profile change.
struct PointerReport {
    int16_t x = 0, y = 0;
    int8_t wheel = 0, pan = 0;
    uint8_t buttons = 0;
};
struct PointerConfig {
    uint16_t gain_percent = 100;
    bool invert_x = false, invert_y = false, swap_axes = false;
};
class OptionalInput {
public:
    void Hardware(unsigned trackballs, unsigned encoders) {
        trackballs_ = std::min(trackballs, 2u);
        encoders_ = std::min(encoders, 2u);
        Reset();
    }
    unsigned Trackballs() const { return trackballs_; }
    unsigned Encoders() const { return encoders_; }
    bool Configure(PointerConfig config) {
        if (config.gain_percent < 10 || config.gain_percent > 800)
            return false;
        config_ = config;
        return true;
    }
    void Reset() { queue_.clear(); }
    bool Motion(unsigned device, int16_t x, int16_t y, uint8_t buttons) {
        if (device >= trackballs_ || (buttons & ~0x1fu))
            return false;
        int32_t dx = x, dy = y;
        if (config_.swap_axes)
            std::swap(dx, dy);
        if (config_.invert_x)
            dx = -dx;
        if (config_.invert_y)
            dy = -dy;
        PointerReport report;
        report.x = Scale(dx);
        report.y = Scale(dy);
        report.buttons = buttons;
        return Push(report);
    }
    bool Rotate(unsigned device, int8_t steps, bool horizontal = false) {
        if (device >= encoders_ || !steps)
            return false;
        PointerReport report;
        if (horizontal)
            report.pan = steps;
        else
            report.wheel = steps;
        return Push(report);
    }
    bool Pop(PointerReport& report) {
        if (queue_.empty())
            return false;
        report = queue_.front();
        queue_.pop_front();
        return true;
    }

private:
    int16_t Scale(int32_t value) const {
        return static_cast<int16_t>(
            std::clamp(value * config_.gain_percent / 100, int32_t{-32767}, int32_t{32767}));
    }
    bool Push(PointerReport report) {
        if (queue_.size() >= 64)
            return false;
        queue_.push_back(report);
        return true;
    }
    unsigned trackballs_ = 0, encoders_ = 0;
    PointerConfig config_;
    std::deque<PointerReport> queue_;
};
}  // namespace cube_dongle

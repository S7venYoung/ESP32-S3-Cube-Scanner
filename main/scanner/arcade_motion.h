#pragma once
#include <algorithm>
#include <cstdint>
inline unsigned ArcadeIdleFrame(uint32_t now, int side) {
    constexpr unsigned sequence[] = {0, 1, 0, 2};
    return sequence[((now + (side ? 190 : 0)) / 260) % 4];
}
inline int ArcadeEase(int t, int duration, int distance) {
    const int p = std::clamp(t * 256 / std::max(1, duration), 0, 256);
    return distance * p * p * (768 - 2 * p) / (256 * 256 * 256);
}
// WPM selects cadence, not a pose. A started action always gets a recovery.
struct ArcadeMotion {
    uint32_t last_ms = 0, elapsed = 0, charge = 0, action_ms = 0, strike_age = 0;
    unsigned attacks = 0;
    int pose = 0, strike = 0, phase = 0, x = 0, y = 0;
    bool super = false;
    void Reset(uint32_t now) {
        *this = {};
        last_ms = now;
    }
    void Step(uint32_t now, int wpm, bool online) {
        const uint32_t delta = std::min<uint32_t>(now - last_ms, 120);
        last_ms = now;
        if (!online) {
            Reset(now);
            return;
        }
        elapsed += delta;
        if (wpm >= 70)
            charge = std::min<uint32_t>(charge + delta, 4000);
        else
            charge = 0;
        const uint32_t period = std::clamp(1400 - std::max(0, wpm) * 8, 380, 1400);
        if (!phase && wpm >= 5 && elapsed >= period) {
            elapsed = 0;
            ++attacks;
            super = charge >= 4000;
            strike = super ? 5 : 1 + ((attacks - 1) % (wpm >= 45 ? 4 : wpm >= 20 ? 2 : 1));
            if (super)
                charge = 0;
            action_ms = 0;
            phase = 1;
        } else if (phase)
            action_ms += delta;
        const uint32_t windup = super ? 160 : 100;
        const uint32_t active = super ? 280 : 180;
        const uint32_t recovery = 200;
        pose = 0;
        x = 0;
        y = 0;
        strike_age = 0;
        if (phase) {
            if (action_ms < windup) {
                phase = 1;
                x = -ArcadeEase(action_ms, windup, 3);
                y = ArcadeEase(action_ms, windup, 2);
            } else if (action_ms < windup + active) {
                phase = 2;
                pose = strike;
                strike_age = action_ms - windup;
                x = -3 + ArcadeEase(strike_age, 80, 11);
                if (strike == 4)
                    y = -ArcadeEase(strike_age, active, 9);
            } else if (action_ms < windup + active + recovery) {
                phase = 3;
                const int t = action_ms - windup - active;
                x = 8 - ArcadeEase(t, recovery, 8);
                if (strike == 4)
                    y = -9 + ArcadeEase(t, recovery, 9);
            } else {
                phase = 0;
                super = false;
            }
        }
        if (!phase) {
            const int t = (now + 190) % 1040;
            y = t < 520 ? ArcadeEase(t, 520, 2) : 2 - ArcadeEase(t - 520, 520, 2);
            if (wpm < 5)
                elapsed = 0;
        }
    }
};

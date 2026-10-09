#pragma once
#include <algorithm>
#include <cstdint>
inline unsigned ArcadeIdleFrame(uint32_t now,int side) {
    // Asymmetric timing keeps the two guards from breathing in lockstep.
    constexpr unsigned sequence[]={0,1,0,2};
    return sequence[((now+(side?190:0))/260)%4];
}

// Visual cadence only; keyboard batteries are never consumed by attacks.
struct ArcadeMotion {
    uint32_t last_ms = 0, elapsed = 0, charge = 0;
    unsigned attacks = 0;
    int pose = 0;
    bool super = false;
    void Reset(uint32_t now) { last_ms = now; elapsed = charge = attacks = 0; pose = 0; super = false; }
    void Step(uint32_t now, int wpm, bool online) {
        const uint32_t delta = std::min<uint32_t>(now - last_ms, 200);
        last_ms = now;
        if (!online || wpm < 5) { Reset(now); return; }
        elapsed += delta;
        if (wpm >= 70) charge = std::min<uint32_t>(charge + delta, 4000);
        else charge = 0;
        const uint32_t period = std::clamp(1400 - wpm * 8, 380, 1400);
        if (elapsed >= period) {
            elapsed %= period;
            ++attacks;
            super = charge >= 4000;
            if (super) { pose = 5; charge = 0; }
            else pose = 1 + ((attacks - 1) % (wpm >= 45 ? 4 : wpm >= 20 ? 2 : 1));
        }
        // Active strike followed by recovery/guard; no permanent pose lock.
        if (elapsed > period * 2 / 3) { pose = 0; super = false; }
    }
};

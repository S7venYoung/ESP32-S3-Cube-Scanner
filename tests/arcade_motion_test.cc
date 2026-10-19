#include "../main/scanner/arcade_motion.h"
#include <cassert>
int main() {
    // Boundary tests use real, unscaled WPM values.
    auto poses_at = [](int wpm) {
        ArcadeMotion motion;
        unsigned seen = 0;
        for (uint32_t now = 40; now <= 16000; now += 40) {
            motion.Step(now, wpm, true);
            seen |= 1u << motion.pose;
        }
        return seen;
    };
    assert(poses_at(2) == 1);
    assert((poses_at(3) & (1u << 1)) && !(poses_at(3) & (1u << 2)));
    assert(!(poses_at(9) & (1u << 2)));
    assert(poses_at(10) & (1u << 2));
    assert(!(poses_at(24) & ((1u << 3) | (1u << 4))));
    assert((poses_at(25) & ((1u << 3) | (1u << 4))) == ((1u << 3) | (1u << 4)));
    assert(!(poses_at(39) & (1u << 5)));
    assert(poses_at(40) & (1u << 5));
    ArcadeMotion charged;
    uint32_t first_super = 0;
    for (uint32_t now = 40; now <= 4200; now += 40) {
        charged.Step(now, 40, true);
        if (charged.super && !first_super)
            first_super = now;
        if (now < 3000)
            assert(!charged.super);
    }
    assert(first_super >= 3000 && first_super <= 4200);
    charged.Step(4240, 0, false);
    assert(!charged.super && charged.charge== 0 && charged.pose== 0);
    ArcadeMotion left, right;
    assert(ArcadeIdleFrame(0, 0) == 0);
    assert(ArcadeIdleFrame(260, 0) == 1);
    assert(ArcadeIdleFrame(520, 0) == 0);
    assert(ArcadeIdleFrame(780, 0) == 2);
    assert(ArcadeIdleFrame(1040, 0) == 0);
    assert(ArcadeIdleFrame(100, 0) != ArcadeIdleFrame(100, 1));
    left.Reset(0);
    right.Reset(0);
    bool super = false, punch = false, kick = false, fireball = false, uppercut = false;
    for (uint32_t now = 80; now <= 16000; now += 80) {
        left.Step(now, 100, true);
        right.Step(now, 0, true);
        assert(right.pose == 0);
        assert(left.pose >= 0 && left.pose <= 5);
        super |= left.pose == 5;
        punch |= left.pose == 1;
        kick |= left.pose == 2;
        fireball |= left.pose == 3;
        uppercut |= left.pose == 4;
    }
    assert(super && punch && kick && fireball && uppercut);
    left.Step(16080, 100, false);
    assert(left.pose == 0 && left.charge == 0);
    left.Reset(UINT32_MAX - 20);
    left.Step(50, 70, true);
    assert(left.elapsed == 71);
    // A sampled WPM drop must not cancel a strike halfway through recovery.
    left.Reset(0);
    for (uint32_t now = 40; now <= 720; now += 40)
        left.Step(now, 100, true);
    assert(left.phase != 0);
    const unsigned started = left.attacks;
    bool recovery = false;
    for (uint32_t now = 760; now <= 1600; now += 40) {
        left.Step(now, 0, true);
        recovery |= left.phase == 3;
        assert(left.attacks == started);
        assert(left.x >= -3 && left.x <= 8 && left.y >= -9 && left.y <= 2);
    }
    assert(recovery && left.phase == 0 && left.pose == 0);
    assert(ArcadeEase(0, 100, 8) == 0 && ArcadeEase(100, 100, 8) == 8);
}

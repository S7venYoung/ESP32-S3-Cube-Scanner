#include "../main/scanner/fight_telemetry.h"
#include <cassert>
#include "../main/scanner/arcade_motion.h"
int main() {
    uint8_t packet[17] = {255, 255, 0xab, 0xce, 1, 7, 1, 2, 3, 4, 30, 70, 72, 100, 0x88, 1, 0};
    FightTelemetry value;
    assert(ParseFightTelemetry(packet, 17, value));
    assert(value.left_wpm == 30 && value.right_wpm == 70 && value.left_battery == 72 &&
           value.right_battery == 100);
    assert(value.modifiers == 4 && value.keyboard_id == 0x04030201);
    packet[5] = 1;
    assert(ParseFightTelemetry(packet, 17, value) && value.left_battery == -1);
    packet[10] = 0;
    packet[11] = 255;
    assert(ParseFightTelemetry(packet, 17, value) && value.left_wpm == 0 && value.right_wpm == 255);
    assert(!ParseFightTelemetry(packet, 16, value));
    packet[4] = 2;
    assert(!ParseFightTelemetry(packet, 17, value));
    packet[4] = 1;
    packet[5] = 7;
    packet[12] = 101;
    assert(!ParseFightTelemetry(packet, 17, value));
    assert(FightActionForWpm(-1) == 0 && FightActionForWpm(2) == 0 && FightActionForWpm(3) == 1);
    assert(FightActionForWpm(9) == 1 && FightActionForWpm(10) == 2 && FightActionForWpm(39) == 2 && FightActionForWpm(40) == 3);
    FightTelemetry previous, next;
    previous.sequence = 65535;
    next.sequence = 0;
    assert(FightFrameNewer(previous, next, true));
    previous.sequence = 12;
    next.sequence = 12;
    assert(!FightFrameNewer(previous, next, true));
    assert(!FightFrameNewer(previous, next, false));
    next.sequence = 11;
    assert(!FightFrameNewer(previous, next, true));
    assert(FightFrameNewer(previous, next, false));
    // Independently decoded bytes drive distinct actors, not total-WPM / 2.
    packet[5] = 1;
    packet[10] = 100;
    packet[11] = 0;
    assert(ParseFightTelemetry(packet, 17, value));
    ArcadeMotion left, right;
    bool super_seen = false;
    for (uint32_t now = 100; now <= 6000; now += 100) {
        left.Step(now, value.left_wpm, value.online);
        right.Step(now, value.right_wpm, value.online);
        super_seen |= left.super;
    }
    assert(left.attacks > 0 && super_seen && right.attacks == 0);
    packet[10] = 0;
    packet[11] = 100;
    assert(ParseFightTelemetry(packet, 17, value));
    left.Reset(0);
    right.Reset(0);
    for (uint32_t now = 100; now <= 6000; now += 100) {
        left.Step(now, value.left_wpm, value.online);
        right.Step(now, value.right_wpm, value.online);
    }
    assert(left.attacks == 0 && right.attacks > 0);
    left.Step(6100, 0, true);
    right.Step(6100, 0, true);
    for (uint32_t now = 6200; now <= 7200; now += 100) {
        left.Step(now, 0, true);
        right.Step(now, 0, true);
    }
    assert(left.pose == 0 && right.pose == 0);
    // A radio gap must not turn keyboard presence/battery into unknown.
    FightTelemetry last;
    last.online = true;
    last.sequence = 42;
    last.left_wpm = 100;
    last.right_wpm = 70;
    last.left_battery = 0;
    last.right_battery = 79;
    last.modifiers = 4;
    constexpr int64_t seen = 1000000;
    auto sample = FightSnapshotAt(last, seen, seen + kFightInputLeaseUs);
    assert(sample.online && sample.left_wpm == 100 && sample.modifiers == 4);
    sample = FightSnapshotAt(last, seen, seen + kFightInputLeaseUs + 1);
    assert(sample.online && sample.left_battery == 0 && sample.right_battery == 79);
    assert(sample.left_wpm == 0 && sample.right_wpm == 0 && sample.modifiers == 0);
    left.Step(7300, sample.left_wpm, sample.online);
    right.Step(7300, sample.right_wpm, sample.online);
    assert(left.pose == 0 && right.pose == 0);
    assert(sample.sequence == 42 && !FightFrameNewer(last, sample, false));
    assert(last.left_wpm == 100 && last.modifiers == 4);  // Snapshot is not destructive.
    assert(FightSnapshotAt(last, seen, seen + kFightPresenceLeaseUs).online);
    sample = FightSnapshotAt(last, seen, seen + kFightPresenceLeaseUs + 1);
    assert(!sample.online && sample.left_battery == -1 && sample.right_battery == -1);
    assert(!FightSnapshotAt(last, -1, seen).online);
    assert(!FightSnapshotAt(last, seen, seen - 1).online);
    // A genuinely new frame resumes input immediately.
    last.sequence++;
    sample = FightSnapshotAt(last, seen + 5000000, seen + 5000000);
    assert(sample.online && sample.left_wpm == 100 && sample.modifiers == 4);
}

#pragma once
#include <cstddef>
#include <cstdint>

struct FightTelemetry {
    uint32_t keyboard_id = 0;
    uint16_t sequence = 0;
    int left_wpm = -1, right_wpm = -1;
    int left_battery = -1, right_battery = -1;
    uint8_t modifiers = 0;
    bool online = false;
};
inline bool ParseFightTelemetry(const uint8_t* data, size_t length, FightTelemetry& result) {
    if (!data || length != 17 || data[0] != 255 || data[1] != 255 || data[2] != 0xab ||
        data[3] != 0xce || data[4] != 1 || (data[5] & ~7u) || !(data[5] & 1) ||
        ((data[5] & 2) && data[12] > 100) || ((data[5] & 4) && data[13] > 100))
        return false;
    FightTelemetry value;
    value.keyboard_id = uint32_t(data[6]) | uint32_t(data[7]) << 8 | uint32_t(data[8]) << 16 |
                        uint32_t(data[9]) << 24;
    value.sequence = uint16_t(data[15]) | uint16_t(data[16]) << 8;
    value.left_wpm = data[10];
    value.right_wpm = data[11];
    if (data[5] & 2)
        value.left_battery = data[12];
    if (data[5] & 4)
        value.right_battery = data[13];
    value.modifiers = ((data[14] & 0x11) ? 1 : 0) | ((data[14] & 0x44) ? 2 : 0) |
                      ((data[14] & 0x88) ? 4 : 0) | ((data[14] & 0x22) ? 8 : 0);
    value.online = true;
    result = value;
    return true;
}
inline uint8_t FightActionForWpm(int wpm) {
    return wpm >= 70 ? 3 : wpm >= 30 ? 2 : wpm >= 5 ? 1 : 0;
}
// Sequence is a wrapping 16-bit counter. Repeated radio copies must not
// keep old typing speeds alive indefinitely; a fresh session may restart it.
inline bool FightFrameNewer(const FightTelemetry& previous, const FightTelemetry& next,
                            bool previous_live) {
    const auto delta = uint16_t(next.sequence - previous.sequence);
    return delta != 0 && (!previous_live || delta < 0x8000);
}
FightTelemetry GetFightTelemetry();

// Presence/battery tolerate missed radio windows; input must never stay held.
constexpr int64_t kFightInputLeaseUs = 3000000;
constexpr int64_t kFightPresenceLeaseUs = 90000000;
inline FightTelemetry FightSnapshotAt(const FightTelemetry& last, int64_t seen, int64_t now) {
    if (seen < 0 || now < seen || now - seen > kFightPresenceLeaseUs)
        return {};
    auto snapshot = last;
    if (now - seen > kFightInputLeaseUs) {
        snapshot.left_wpm = 0;
        snapshot.right_wpm = 0;
        snapshot.modifiers = 0;
    }
    return snapshot;
}

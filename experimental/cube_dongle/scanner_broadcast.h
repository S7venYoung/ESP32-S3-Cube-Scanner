#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace cube_dongle {
// Prospector v1 (ABCD) and split-WPM v1 (ABCE), both legacy AD payloads.
struct ScannerState {
    uint32_t id = 0;
    uint16_t sequence = 0;
    uint8_t layer = 0, profile = 0, modifiers = 0;
    const char* layer_name = "";
    int left_battery = -1, right_battery = -1;
    int wpm = 0, left_wpm = 0, right_wpm = 0;
    bool usb = false, ble = false;
    uint8_t links = 0;
};
inline uint8_t ScannerByte(int value) {
    return value < 0 ? 0 : value > 255 ? 255 : static_cast<uint8_t>(value);
}
inline uint8_t ScannerHidModifiers(uint8_t mac) {
    return ((mac & 1) ? 1 : 0) | ((mac & 2) ? 4 : 0) |
           ((mac & 4) ? 8 : 0) | ((mac & 8) ? 2 : 0);
}
inline std::array<uint8_t, 31> ScannerStatusPacket(const ScannerState& s) {
    std::array<uint8_t, 31> ad{2, 1, 6, 27, 0xff};
    auto* p = ad.data() + 5;
    p[0] = p[1] = 0xff; p[2] = 0xab; p[3] = 0xcd; p[4] = 1;
    p[5] = s.left_battery >= 0 && s.left_battery <= 100 ? s.left_battery : 255;
    p[6] = s.layer & 15; p[7] = s.profile < 5 ? s.profile : 0;
    p[8] = s.links; p[9] = (s.usb ? 12 : 0) | (s.ble ? 16 : 0);
    p[10] = 1;
    p[12] = s.right_battery > 0 && s.right_battery <= 100 ? s.right_battery : 0;
    for (unsigned i = 0; i < 4 && s.layer_name[i]; ++i) p[15 + i] = s.layer_name[i];
    for (unsigned i = 0; i < 4; ++i) p[19 + i] = s.id >> (8 * i);
    p[23] = ScannerHidModifiers(s.modifiers); p[24] = ScannerByte(s.wpm);
    return ad;
}
inline std::array<uint8_t, 22> ScannerFightPacket(const ScannerState& s) {
    std::array<uint8_t, 22> ad{2, 1, 6, 18, 0xff};
    auto* p = ad.data() + 5;
    p[0] = p[1] = 0xff; p[2] = 0xab; p[3] = 0xce; p[4] = 1;
    p[5] = 1;
    for (unsigned i = 0; i < 4; ++i) p[6 + i] = s.id >> (8 * i);
    p[10] = ScannerByte(s.left_wpm); p[11] = ScannerByte(s.right_wpm);
    if (s.left_battery >= 0 && s.left_battery <= 100) { p[5] |= 2; p[12] = s.left_battery; }
    if (s.right_battery >= 0 && s.right_battery <= 100) { p[5] |= 4; p[13] = s.right_battery; }
    p[14] = ScannerHidModifiers(s.modifiers);
    p[15] = s.sequence; p[16] = s.sequence >> 8;
    return ad;
}
}  // namespace cube_dongle

#pragma once
#include <cstddef>
#include <cstdint>

// Prospector packed 26-byte manufacturer payload. Decode bytes instead of
// casting an unaligned radio buffer. Source: prospector-zmk-module v2.2.3,
// include/zmk/status_advertisement.h (MIT).
inline bool ParseProspectorModifiers(const uint8_t* data, size_t size, uint8_t& mac) {
    if (!data || size != 26 || data[0] != 0xff || data[1] != 0xff ||
        data[2] != 0xab || data[3] != 0xcd ||
        (data[4] != 1 && (data[4] >> 4) != 2) || data[10] > 2) return false;
    // A peripheral doesn't own the complete keyboard modifier state.
    if (data[10] == 2) return false;
    const auto hid = data[23];
    mac = ((hid & 0x11) ? 1 : 0) | ((hid & 0x44) ? 2 : 0) |
          ((hid & 0x88) ? 4 : 0) | ((hid & 0x22) ? 8 : 0);
    return true;
}

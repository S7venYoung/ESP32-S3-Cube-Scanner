#pragma once
#include "prospector_modifiers.h"
#include <cstring>

struct KeyboardStatus {
    bool online = false;
    uint8_t active_layer = 0;
    char layer[17] = {};
    int profile = -1, wpm = -1;
    bool usb = false, ble = false;
    uint8_t modifiers = 0;
    int left_battery = -1, right_battery = -1;
};
inline bool ParseKeyboardStatus(const uint8_t* data, size_t size, KeyboardStatus& out) {
    uint8_t mods;
    if (!ParseProspectorModifiers(data, size, mods) || data[6] > 15 || (data[7] & 7) > 4)
        return false;
    KeyboardStatus value;
    value.online = true;
    value.active_layer = data[6];
    value.modifiers = mods;
    std::memcpy(value.layer, data + 15, 4);
    for (int i = 0; i < 4; ++i) {
        if (value.layer[i] == 0) break;
        if (static_cast<unsigned char>(value.layer[i]) < 32 ||
            static_cast<unsigned char>(value.layer[i]) > 126) value.layer[i] = '?';
    }
    value.profile = (data[7] & 7) + 1;
    value.wpm = data[24];
    value.usb = (data[9] & 8) != 0;
    value.ble = (data[9] & 16) != 0;
    if (data[10] == 1) {
        if (data[5] <= 100) value.left_battery = data[5];
        // The original advertisement reserves peripheral zero for unavailable.
        if (data[12] > 0 && data[12] <= 100) value.right_battery = data[12];
    }
    out = value;
    return true;
}
KeyboardStatus GetKeyboardStatus();


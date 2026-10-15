#pragma once
#include "fight_telemetry.h"
#include "keyboard_status.h"

// Do not infer layer/profile/total WPM from a fight-only advertisement.
struct KeyboardThemeState {
    bool online = false;
    const char* transport = "--";
    const char* layer = "--";
    int profile = -1, wpm = -1;
    int left_battery = -1, right_battery = -1;
    uint8_t modifiers = 0;
};
inline bool ThemeBatteryValid(int value) { return value >= 0 && value <= 100; }
inline KeyboardThemeState KeyboardThemeSnapshot(const KeyboardStatus& kb,
                                                const FightTelemetry& fight) {
    KeyboardThemeState s;
    s.online = kb.online || fight.online;
    s.transport = kb.online      ? (kb.usb   ? "USB"
                                    : kb.ble ? "BLE"
                                             : "OFF")
                  : fight.online ? "ZMK"
                                 : "--";
    if (kb.online) {
        s.layer = kb.layer[0] ? kb.layer : "--";
        s.profile = kb.profile;
        s.wpm = kb.wpm;
    }
    s.modifiers = fight.online ? fight.modifiers : kb.online ? kb.modifiers : 0;
    const int a[] = {fight.left_battery, fight.right_battery};
    const int b[] = {kb.left_battery, kb.right_battery};
    int* output[] = {&s.left_battery, &s.right_battery};
    for (int i = 0; i < 2; ++i)
        *output[i] = fight.online && ThemeBatteryValid(a[i]) ? a[i]
                     : kb.online && ThemeBatteryValid(b[i])  ? b[i]
                                                             : -1;
    return s;
}

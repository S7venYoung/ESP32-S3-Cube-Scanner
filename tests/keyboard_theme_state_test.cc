#include "../main/scanner/keyboard_theme_state.h"
#include <cassert>
#include <cstring>

int main() {
    KeyboardStatus kb;
    FightTelemetry fight;
    auto s = KeyboardThemeSnapshot(kb, fight);
    assert(!s.online && s.wpm == -1 && s.profile == -1);
    assert(s.left_battery == -1 && s.right_battery == -1 && !s.modifiers);
    kb.online = true;
    kb.ble = true;
    kb.profile = 3;
    kb.wpm = 42;
    kb.modifiers = 4;
    std::strcpy(kb.layer, "BASE");
    kb.left_battery = 72;
    kb.right_battery = 100;
    s = KeyboardThemeSnapshot(kb, fight);
    assert(s.online && !std::strcmp(s.transport, "BLE"));
    assert(!std::strcmp(s.layer, "BASE") && s.profile == 3 && s.wpm == 42);
    assert(s.left_battery == 72 && s.right_battery == 100 && s.modifiers == 4);
    kb.usb = true;
    assert(!std::strcmp(KeyboardThemeSnapshot(kb, fight).transport, "USB"));
    fight.online = true;
    fight.left_battery = 0;
    fight.right_battery = -1;
    fight.left_wpm = 100;
    fight.right_wpm = 10;
    fight.modifiers = 1;
    s = KeyboardThemeSnapshot(kb, fight);
    assert(s.left_battery == 0 && s.right_battery == 100 && s.modifiers == 1);
    assert(s.wpm == 42);  // Never invent total WPM from independent counters.
    fight.left_battery = 101;
    assert(KeyboardThemeSnapshot(kb, fight).left_battery == 72);
    kb.online = false;
    s = KeyboardThemeSnapshot(kb, fight);
    assert(s.online && !std::strcmp(s.transport, "ZMK"));
    assert(s.wpm == -1 && s.profile == -1 && !std::strcmp(s.layer, "--"));
    assert(s.left_battery == -1 && s.right_battery == -1);
    fight.online = false;
    s = KeyboardThemeSnapshot(kb, fight);
    assert(!s.online && !s.modifiers && s.wpm == -1 && s.left_battery == -1);
}

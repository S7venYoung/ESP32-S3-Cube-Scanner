#include "../experimental/cube_dongle/scanner_broadcast.h"
#include "../main/scanner/keyboard_status.h"
#include "../main/scanner/fight_telemetry.h"
#include <cassert>
int main() {
    cube_dongle::ScannerState s;
    s.id = 0x12345678; s.sequence = 0xabcd; s.layer = 3;
    s.profile = 4; s.layer_name = "LOWER"; s.modifiers = 15;
    s.left_battery = 0; s.right_battery = 100;
    s.wpm = 300; s.left_wpm = 12; s.right_wpm = 30; s.usb = true;
    auto a = cube_dongle::ScannerStatusPacket(s);
    KeyboardStatus k;
    assert(a.size() == 31 && a[3] == 27);
    assert(ParseKeyboardStatus(a.data() + 5, 26, k));
    assert(k.usb && !k.ble && k.profile == 5 && k.wpm == 255);
    assert(k.modifiers == 15 && k.left_battery == 0 && k.right_battery == 100);
    assert(std::strcmp(k.layer, "LOWE") == 0 && a[11] == 3);
    auto f = cube_dongle::ScannerFightPacket(s);
    FightTelemetry t;
    assert(ParseFightTelemetry(f.data() + 5, 17, t));
    assert(t.keyboard_id == s.id && t.sequence == s.sequence);
    assert(t.left_wpm == 12 && t.right_wpm == 30 && t.modifiers == 15);
    assert(t.left_battery == 0 && t.right_battery == 100);
    s.left_battery = s.right_battery = -1;
    s.left_wpm = -1; s.right_wpm = 1000; s.layer_name = "";
    a = cube_dongle::ScannerStatusPacket(s);
    assert(ParseKeyboardStatus(a.data() + 5, 26, k));
    assert(k.left_battery == -1 && k.right_battery == -1 && !k.layer[0]);
    f = cube_dongle::ScannerFightPacket(s);
    assert(ParseFightTelemetry(f.data() + 5, 17, t));
    assert(t.left_battery == -1 && t.right_battery == -1);
    assert(t.left_wpm == 0 && t.right_wpm == 255);
    for (unsigned i = 0; i < 16; ++i) {
        s.modifiers = i; f = cube_dongle::ScannerFightPacket(s);
        assert(ParseFightTelemetry(f.data() + 5, 17, t) && t.modifiers == i);
    }
}

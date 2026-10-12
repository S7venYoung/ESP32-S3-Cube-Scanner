#include "../main/scanner/keyboard_status.h"
#include "../main/scanner/dashboard_command.h"
#include <cassert>
int main() {
    uint8_t d[26] = {255,255,0xab,0xcd,0x22,72,0,0x18,1,16,1,0,100,0,0,'B','A','S',0};
    d[24] = 106;
    KeyboardStatus value;
    assert(ParseKeyboardStatus(d, 26, value));
    assert(value.online && value.profile == 1 && value.wpm == 106 && value.ble && !value.usb);
    assert(std::strcmp(value.layer,"BAS") == 0 && value.left_battery == 72 && value.right_battery == 100);
    d[5] = 0; d[12] = 0;
    assert(ParseKeyboardStatus(d, 26, value));
    assert(value.left_battery == 0 && value.right_battery == -1);
    d[10] = 2; assert(!ParseKeyboardStatus(d, 26, value));
    d[10] = 1; d[7] = 5; assert(!ParseKeyboardStatus(d, 26, value));
    d[7] = 0; assert(!ParseKeyboardStatus(d, 25, value));
    assert(DashboardVoiceIntent("你好小智，切换白色主题") == DashboardVoiceCommand::White);
    assert(DashboardVoiceIntent("切换简约主题") == DashboardVoiceCommand::White);
}

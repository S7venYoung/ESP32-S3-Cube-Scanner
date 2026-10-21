#pragma once

namespace cube_dongle {
// Zero means USB output; 1..5 are the existing computer BLE profiles.
// USB does not create or delete a bond and is not a sixth BLE profile.
constexpr unsigned NextHostCycleSlot(bool usb_selected, unsigned ble_profile) {
    if (usb_selected || ble_profile > 4)
        return 1;
    return ble_profile == 4 ? 0 : ble_profile + 2;
}
}  // namespace cube_dongle

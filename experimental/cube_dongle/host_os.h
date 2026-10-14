#pragma once
#include <cstdint>
namespace cube_dongle {
// Observation-only USB enumeration signatures. Apple USB stacks cannot
// distinguish macOS from iOS; Linux cannot distinguish Android. Never guess
// from a serial number, local Mac state, or the keyboard's BLE address.
inline unsigned ClassifyUsbOs(uint32_t flags) {
    if ((flags & 3) == 3)
        return 2;  // short string probe plus full reread: Apple
    if (flags & 8)
        return 1;  // one-shot BOS request at wLength=255: Windows
    if ((flags & 4) && !(flags & 1))
        return 3;  // full string requests without short probes: Linux
    return 0;
}
inline uint32_t UsbDescriptorObservation(uint8_t descriptor, uint16_t length) {
    if (descriptor == 3)
        return length == 2 ? 1u : length == 255 ? 4u : 2u;
    if (descriptor == 15)
        return length == 255 ? 8u : 16u;
    return 0;
}
}  // namespace cube_dongle

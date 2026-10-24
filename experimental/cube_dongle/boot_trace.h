#pragma once
#include <cstdint>
namespace cube_dongle {
struct BootTrace {
    uint32_t magic, boot, stage, checksum;
    bool Valid() const { return magic == 0x43554254 && checksum == (magic ^ boot ^ stage ^ 0x5ac3817d); }
    void Set(uint32_t sequence, uint32_t value) {
        magic = 0x43554254;
        boot = sequence;
        stage = value;
        checksum = magic ^ boot ^ stage ^ 0x5ac3817d;
    }
};
}  // namespace cube_dongle

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cube_dongle {
// Public pair-set identifier, NOT a secret or MITM authentication credential.
constexpr std::array<uint8_t, 16> kPairSet = {0x43, 0x56, 0x50, 0x31, 0x6f, 0x37, 0x4d, 0x91,
                                              0xa6, 0x2e, 0x88, 0x05, 0xd4, 0x73, 0x9b, 0xc0};
inline int AutoSide(const uint8_t* data, size_t length) {
    if (!data)
        return -1;
    int side = -1;
    for (size_t at = 0; at < length;) {
        const size_t size = data[at++];
        if (!size)
            break;
        if (size > length - at)
            return -1;
        if (data[at] == 0x21 && size == 19 && !std::memcmp(data + at + 1, kPairSet.data(), 16)) {
            if (side != -1 || data[at + 17] != 1 || data[at + 18] > 1)
                return -1;
            side = data[at + 18];
        }
        at += size;
    }
    return side;
}
inline bool AutoIdentity(const uint8_t* data, size_t length, size_t side) {
    return data && length == 18 && side < 2 && data[0] == 1 && data[1] == side &&
           !std::memcmp(data + 2, kPairSet.data(), 16);
}
struct AutoCandidate {
    std::array<uint8_t, 7> identity{};
    bool seen = false, ambiguous = false;
    void Observe(const uint8_t* bytes, uint8_t type) {
        std::array<uint8_t, 7> value{};
        value[0] = type;
        std::memcpy(value.data() + 1, bytes, 6);
        if (seen && value != identity)
            ambiguous = true;
        else {
            seen = true;
            identity = value;
        }
    }
};
}  // namespace cube_dongle

#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace cube_dongle {
struct PeerBinding {
    std::array<uint8_t, 6> bytes{};  // NimBLE little-endian address bytes
    uint8_t type = 0;
};
inline bool ParseIdentity(const std::string& address, int type, PeerBinding& result) {
    if (address.size() != 17 || (type != 0 && type != 1))
        return false;
    PeerBinding candidate;
    candidate.type = type;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    bool zero = true, ones = true;
    for (size_t i = 0; i < 6; ++i) {
        const int high = hex(address[i * 3]), low = hex(address[i * 3 + 1]);
        if (high < 0 || low < 0 || (i < 5 && address[i * 3 + 2] != ':'))
            return false;
        const auto byte = static_cast<uint8_t>((high << 4) | low);
        candidate.bytes[5 - i] = byte;
        zero &= byte == 0;
        ones &= byte == 255;
    }
    if (zero || ones || (type == 1 && (candidate.bytes[5] & 0xc0) != 0xc0))
        return false;
    result = candidate;
    return true;
}
struct BindRequest {
    size_t side = 0;
    std::string stored;
    PeerBinding identity;
};
inline bool ParseBindCommand(const std::string& command, BindRequest& result) {
    // Exact USB command, no shell interpolation or arbitrary NVS keys.
    if (command.size() != 33 || command.compare(0, 12, "DONGLE BIND ") != 0 ||
        (command[12] != 'L' && command[12] != 'R') || command[13] != ' ' || command[31] != ' ' ||
        (command[32] != '0' && command[32] != '1'))
        return false;
    BindRequest candidate;
    candidate.side = command[12] == 'R';
    candidate.stored = command.substr(14);
    if (!ParseIdentity(command.substr(14, 17), command[32] - '0', candidate.identity))
        return false;
    result = candidate;
    return true;
}
}  // namespace cube_dongle

#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include "keyboard_dashboard_data.h"

namespace cube_dongle {
inline bool ParseHostMetricsFrame(const char* line, std::array<uint8_t, DASH_DATA_SIZE>& out) {
    if (!line || std::strlen(line) != 6 + DASH_DATA_SIZE * 2 || std::strncmp(line, "HOST1 ", 6))
        return false;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    std::array<uint8_t, DASH_DATA_SIZE> next{};
    for (size_t i = 0; i < next.size(); ++i) {
        const int hi = hex(line[6 + i * 2]), lo = hex(line[7 + i * 2]);
        if (hi < 0 || lo < 0)
            return false;
        next[i] = uint8_t(hi * 16 + lo);
    }
    if (!dash_valid(next.data(), next.size()))
        return false;
    out = next;
    return true;
}

// RAM only. The owner serializes access; no NVS/flash writes or BLE calls here.
class KeyboardDashboard {
public:
    bool Update(const uint8_t* data, size_t size, uint64_t now) {
        if (size != DASH_DATA_SIZE || !dash_valid(data, DASH_DATA_SIZE))
            return false;
        std::memcpy(last_.data(), data, size);
        at_ = now;
        seen_ = true;
        return true;
    }
    std::array<uint8_t, DASH_PACKET_SIZE> Packet(unsigned output, unsigned profile,
                                                 uint64_t now) const {
        std::array<uint8_t, DASH_PACKET_SIZE> packet{DASH_PACKET_VERSION, 1};
        if (dash_output_valid(output, profile)) {
            packet[2] = output;
            packet[3] = profile;
        }
        const uint64_t age = now >= at_ ? now - at_ : DASH_HOST_TTL_MS;
        dash_age_data(packet.data() + DASH_PACKET_HEADER_SIZE, last_.data(), seen_,
                      age < UINT32_MAX ? uint32_t(age) : UINT32_MAX);
        return packet;
    }

private:
    std::array<uint8_t, DASH_DATA_SIZE> last_{1};
    uint64_t at_ = 0;
    bool seen_ = false;
};
}  // namespace cube_dongle

#pragma once
#include <cstdint>
#include <cstring>
#include <string>

namespace cube_dongle {
struct RadioPoolCount {
    int free = -1, total = -1;
    uint32_t Packed() const {
        return (uint32_t(uint16_t(total)) << 16) | uint16_t(free);
    }
    static RadioPoolCount Unpack(uint32_t packed) {
        const unsigned free = packed & 0xffff, total = packed >> 16;
        return {free == 0xffff ? -1 : int(free), total == 0xffff ? -1 : int(total)};
    }
    std::string Text() const { return std::to_string(free) + "/" + std::to_string(total); }
};
// Matches ble_hs_conn_can_alloc() in the project's pinned IDF 6.1 NimBLE:
// one connection, three fixed L2CAP channels (ATT/signaling/SMP), and one
// available GATT client-configuration block. Unknown pools stay unknown.
struct RadioPools {
    RadioPoolCount connection, channels, gatt;
    void Observe(const char* name, int free, int total) {
        if (!name || free < 0 || total < 0 || free > total || total >= 65535)
            return;
        RadioPoolCount* pool = nullptr;
        if (!std::strcmp(name, "ble_hs_conn_pool"))
            pool = &connection;
        else if (!std::strcmp(name, "ble_l2cap_chan_pool"))
            pool = &channels;
        else if (!std::strcmp(name, "ble_gatts_clt_cfg_pool"))
            pool = &gatt;
        if (pool)
            *pool = {free, total};
    }
    unsigned Missing() const {
        return (connection.free < 0 ? 1 : 0) | (channels.free < 0 ? 2 : 0) |
               (gatt.free < 0 ? 4 : 0);
    }
    unsigned Blocked() const {
        return (connection.free >= 0 && connection.free < 1 ? 1 : 0) |
               (channels.free >= 0 && channels.free < 3 ? 2 : 0) |
               (gatt.free >= 0 && gatt.free < 1 ? 4 : 0);
    }
};
}  // namespace cube_dongle

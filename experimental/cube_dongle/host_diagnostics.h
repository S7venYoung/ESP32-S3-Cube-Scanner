#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include "radio_pools.h"

namespace cube_dongle {
// Host event queue writes; CDC reads only atomics. No identities or key bytes.
struct HostDiagnostics {
    std::atomic<uint32_t> flags{0}, connects{0}, disconnects{0}, repeats{0};
    std::atomic<uint32_t> split_links{0}, max_links{0};
    std::atomic<uint32_t> pool_connection{0xffffffff}, pool_channels{0xffffffff},
        pool_gatt{0xffffffff}, nimble_limit{0};
    std::atomic<bool> pool_scan_complete{false};
    std::atomic<bool> repair_ready{false};
    std::atomic<int> adv_stage{0}, adv_rc{0}, adv_stop_rc{-1}, connect_rc{-1}, security_rc{-1},
        encryption_rc{-1}, disconnect_reason{-1}, auth_reason{0}, keys{0};
    void NewConnection() {
        ++connects;
        security_rc = encryption_rc = -1;
        auth_reason = keys = 0;
    }
    std::string Text() const {
        const auto f = flags.load();
        const RadioPools pools{RadioPoolCount::Unpack(pool_connection.load()),
                               RadioPoolCount::Unpack(pool_channels.load()),
                               RadioPoolCount::Unpack(pool_gatt.load())};
        return " HOST-DIAG=2 LINK=" + std::to_string(bool(f & 1)) +
               " ENCRYPTED=" + std::to_string(bool(f & 2)) +
               " BONDED=" + std::to_string(bool(f & 4)) +
               " AUTHORIZED=" + std::to_string(bool(f & 8)) +
               " SECURITY-PENDING=" + std::to_string(bool(f & 16)) +
               " KBD-SUB=" + std::to_string(bool(f & 32)) +
               " BOOT-SUB=" + std::to_string(bool(f & 64)) +
               " ADV=" + std::to_string(bool(f & 128)) +
               " PAIR-WINDOW=" + std::to_string(bool(f & 256)) +
               " SLOT-BOUND=" + std::to_string(bool(f & 512)) +
               " REPORT-PROTOCOL=" + std::to_string(bool(f & 1024)) +
               " SUSPENDED=" + std::to_string(bool(f & 2048)) +
               " SPLIT-LINKS=" + std::to_string(split_links.load()) +
               " MAX-LINKS=" + std::to_string(max_links.load()) +
               " ADV-STAGE=" + std::to_string(adv_stage.load()) +
               " ADV-RC=" + std::to_string(adv_rc.load()) +
               " ADV-STOP-RC=" + std::to_string(adv_stop_rc.load()) +
               " CONNECT-RC=" + std::to_string(connect_rc.load()) +
               " SECURITY-RC=" + std::to_string(security_rc.load()) +
               " ENC-RC=" + std::to_string(encryption_rc.load()) +
               " DISCONNECT=" + std::to_string(disconnect_reason.load()) +
               " AUTH-REASON=" + std::to_string(auth_reason.load()) +
               " KEYS=" + std::to_string(keys.load()) +
               " CONNECTS=" + std::to_string(connects.load()) +
               " DISCONNECTS=" + std::to_string(disconnects.load()) +
               " REPEATS=" + std::to_string(repeats.load()) +
               " HOST-EPOCH=3 NIMBLE-LIMIT=" + std::to_string(nimble_limit.load()) +
               " POOL-CONN=" + pools.connection.Text() +
               " POOL-L2CAP=" + pools.channels.Text() +
               " POOL-GATT=" + pools.gatt.Text() +
               " POOL-BLOCKED=" + std::to_string(pools.Blocked()) +
               " POOL-MISSING=" + std::to_string(pools.Missing()) +
               " POOL-SCAN-COMPLETE=" + std::to_string(pool_scan_complete.load()) +
               " REPAIR-READY=" + std::to_string(repair_ready.load());
    }
};
}  // namespace cube_dongle

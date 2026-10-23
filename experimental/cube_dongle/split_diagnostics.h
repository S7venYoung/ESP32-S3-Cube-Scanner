#pragma once
#include <atomic>
#include <cstdint>
#include <string>

namespace cube_dongle {
// Host thread writes, serial task reads. Diagnostic values never contain keys or addresses.
struct SplitDiagnostics {
    std::atomic<uint32_t> phase{0}, advertisements{0}, attempts{0}, last_advertisement{0};
    std::atomic<int> last_error{0}, failed_phase{-1};
    void Advertisement(uint32_t now) {
        last_advertisement = now;
        ++advertisements;
    }
    std::string Text(uint32_t now) const {
        const auto seen = advertisements.load();
        return "phase=" + std::to_string(phase.load()) + " adv=" + std::to_string(seen) +
               " adv_age_ms=" + (seen ? std::to_string(uint32_t(now - last_advertisement.load())) : "-1") +
               " attempts=" + std::to_string(attempts.load()) +
               " error=" + std::to_string(last_error.load()) +
               " failed_phase=" + std::to_string(failed_phase.load());
    }
};
}  // namespace cube_dongle

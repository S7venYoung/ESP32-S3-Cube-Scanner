#pragma once
#include <atomic>
#include <cstdint>
#include <string>

namespace cube_dongle {
// One NimBLE writer, CDC reader. Values are diagnostic observations, not ACKs.
class ScannerTiming {
public:
    void Poll(uint32_t now) {
        const auto old = poll_ms_.exchange(now);
        gap_ms_.store(old ? now - old : 0);
    }
    void Attempt(uint32_t now, uint8_t modifiers) {
        attempt_ms_.store(now);
        modifiers_.store(modifiers);
        attempts_.fetch_add(1);
    }
    void Result(uint32_t now, int rc) {
        result_.store(rc);
        if (rc)
            errors_.fetch_add(1);
        else {
            success_ms_.store(now);
            successes_.fetch_add(1);
        }
    }
    std::string Text(uint32_t now) const {
        const auto attempts = attempts_.load(), successes = successes_.load();
        return " SCAN-DIAG=1 SCAN-CALL-AGE=" + std::to_string(now - poll_ms_.load()) +
               " SCAN-CALL-GAP=" + std::to_string(gap_ms_.load()) +
               " SCAN-ATTEMPTS=" + std::to_string(attempts) +
               " SCAN-OK=" + std::to_string(successes) +
               " SCAN-ERRORS=" + std::to_string(errors_.load()) +
               " SCAN-RC=" + std::to_string(result_.load()) +
               " SCAN-TRY-AGE=" + (attempts ? std::to_string(now - attempt_ms_.load()) : "-1") +
               " SCAN-OK-AGE=" + (successes ? std::to_string(now - success_ms_.load()) : "-1") +
               " SCAN-MODS=" + std::to_string(modifiers_.load());
    }

private:
    std::atomic<uint32_t> poll_ms_{0}, gap_ms_{0}, attempt_ms_{0}, success_ms_{0};
    std::atomic<uint32_t> attempts_{0}, successes_{0}, errors_{0};
    std::atomic<int> result_{-1};
    std::atomic<uint8_t> modifiers_{0};
};
}  // namespace cube_dongle

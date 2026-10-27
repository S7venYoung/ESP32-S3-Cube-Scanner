#pragma once
#include <atomic>
#include <array>
#include <cstdint>
#include <string>

namespace cube_dongle {
// Host thread writes, serial task reads. Diagnostic values never contain keys or addresses.
struct SplitDiagnostics {
    std::atomic<uint32_t> phase{0}, advertisements{0}, attempts{0}, last_advertisement{0};
    std::atomic<int> last_error{0}, failed_phase{-1};
    std::atomic<int> security_start{-1}, encryption_status{-1};
    std::atomic<uint32_t> connections{0}, disconnects{0}, ready_disconnects{0},
        connect_failures{0}, ready_sessions{0}, local_closes{0}, host_resets{0}, positions{0},
        missing_disconnects{0};
    std::atomic<uint32_t> connected_at{0}, ready_at{0}, disconnected_at{0}, position_at{0},
        reset_at{0}, local_close_at{0}, local_close_phase{0}, local_close_kind{0},
        ready_latency{0}, reconnect_latency{0};
    std::atomic<uint32_t> connect_failure_at{0}, connect_failure_phase{0}, connect_failure_kind{0};
    std::atomic<int> connect_failure_error{-1};
    std::atomic<int> disconnect_reason{-1}, disconnect_phase{-1}, reset_reason{-1};
    std::atomic<bool> connected_now{false}, ready_now{false}, has_reconnected{false};
    void ConnectFailed(uint32_t now, int error, unsigned origin_phase, unsigned kind) {
        ++connect_failures;
        connect_failure_at = now;
        connect_failure_error = error;
        connect_failure_phase = origin_phase;
        connect_failure_kind = kind;
    }
    void Connected(uint32_t now) {
        ++connections;
        connected_at = now;
        connected_now = true;
        ready_now = false;
        local_close_kind = 0;
    }
    void Ready(uint32_t now) {
        if (!connected_now || ready_now.exchange(true))
            return;
        ++ready_sessions;
        ready_at = now;
        ready_latency = uint32_t(now - connected_at.load());
        if (recovering_) {
            reconnect_latency = uint32_t(now - recovery_at_);
            has_reconnected = true;
            recovering_ = false;
        }
    }
    void Position(uint32_t now) { position_at = now; ++positions; }
    void LocalClose(uint32_t now, unsigned origin_phase, unsigned kind) {
        ++local_closes;
        local_close_at = now;
        local_close_phase = origin_phase;
        local_close_kind = kind;
    }
    void Disconnected(uint32_t now, int reason, unsigned origin_phase) {
        // This is called only for a matching GAP DISCONNECT, never a retry.
        const auto number = disconnects.fetch_add(1) + 1;
        if (ready_now.exchange(false))
            ++ready_disconnects;
        connected_now = false;
        disconnected_at = now;
        disconnect_reason = reason;
        disconnect_phase = origin_phase;
        if (!recovering_) { recovery_at_ = now; recovering_ = true; }
        auto& event = history_[(number - 1) % history_.size()];
        event.version = number * 2 + 1;
        event.at = now;
        event.reason = reason;
        event.phase = origin_phase;
        event.local = local_close_kind.load();
        event.version = number * 2 + 2;
    }
    void HostReset(uint32_t now, int reason) {
        ++host_resets;
        reset_at = now;
        reset_reason = reason;
        connected_now = ready_now = false;
        local_close_kind = 0;
        // A radio-host reset is separate from a physical-link DISCONNECT.
        recovering_ = false;
    }
    void LinkAlreadyGone() {
        ++missing_disconnects;
        connected_now = ready_now = false;
    }
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
               " failed_phase=" + std::to_string(failed_phase.load()) +
               " security_start=" + std::to_string(security_start.load()) +
               " encryption_status=" + std::to_string(encryption_status.load()) +
               " link_diag=2 connections=" + std::to_string(connections.load()) +
               " disconnects=" + std::to_string(disconnects.load()) +
               " ready_drops=" + std::to_string(ready_disconnects.load()) +
               " connect_failures=" + std::to_string(connect_failures.load()) +
               " connect_fail_error=" + std::to_string(connect_failure_error.load()) +
               " connect_fail_phase=" + std::to_string(connect_failure_phase.load()) +
               " connect_fail_kind=" + std::to_string(connect_failure_kind.load()) +
               " connect_fail_age_ms=" + (connect_failures ? std::to_string(uint32_t(now - connect_failure_at.load())) : "-1") +
               " missing_disconnects=" + std::to_string(missing_disconnects.load()) +
               " last_drop_ms=" + (disconnects ? std::to_string(disconnected_at.load()) : "-1") +
               " drop_age_ms=" + (disconnects ? std::to_string(uint32_t(now - disconnected_at.load())) : "-1") +
               " drop_reason=" + std::to_string(disconnect_reason.load()) +
               " drop_phase=" + std::to_string(disconnect_phase.load()) +
               " link_age_ms=" + (connected_now ? std::to_string(uint32_t(now - connected_at.load())) : "-1") +
               " ready_latency_ms=" + (ready_sessions ? std::to_string(ready_latency.load()) : "-1") +
               " reconnect_ms=" + (has_reconnected ? std::to_string(reconnect_latency.load()) : "-1") +
               " positions=" + std::to_string(positions.load()) +
               " position_age_ms=" + (positions ? std::to_string(uint32_t(now - position_at.load())) : "-1") +
               " local_closes=" + std::to_string(local_closes.load()) +
               " local_kind=" + std::to_string(local_close_kind.load()) +
               " local_phase=" + std::to_string(local_close_phase.load()) +
               " local_age_ms=" + (local_closes ? std::to_string(uint32_t(now - local_close_at.load())) : "-1") +
               " host_resets=" + std::to_string(host_resets.load()) +
               " host_reset_reason=" + std::to_string(reset_reason.load()) +
               " host_reset_age_ms=" + (host_resets ? std::to_string(uint32_t(now - reset_at.load())) : "-1") +
               " drops=" + History();
    }
    std::string History() const {
        std::string out = "{";
        const uint32_t total = disconnects.load();
        const uint32_t count = total < history_.size() ? total : history_.size();
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t number = total - count + 1 + i;
            const auto& event = history_[(number - 1) % history_.size()];
            const auto version = event.version.load();
            const auto at = event.at.load(), phase = event.phase.load(), local = event.local.load();
            const auto reason = event.reason.load();
            if (i) out += ";";
            if (version != number * 2 + 2 || version != event.version.load())
                out += "busy";
            else
                out += std::to_string(number) + ":" + std::to_string(at) + ":" +
                       std::to_string(reason) + ":" + std::to_string(phase) + ":" + std::to_string(local);
        }
        return out + "}";
    }

  private:
    struct Drop {
        std::atomic<uint32_t> version{0}, at{0}, phase{0}, local{0};
        std::atomic<int> reason{-1};
    };
    std::array<Drop, 8> history_{};
    bool recovering_ = false; // Host queue only.
    uint32_t recovery_at_ = 0;
};
}  // namespace cube_dongle

#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include "fight_telemetry.h"
#include "keyboard_status.h"

namespace scanner_policy {
constexpr int64_t kStatusLeaseUs = 90000000;
constexpr int64_t kNameLeaseUs = 180000000;
// Anonymous packets cannot prove a device's advertised name. A nonempty
// filter requires an exact, recently observed name at the same BLE address.
class Names {
public:
    bool Resolve(const std::string& address, const std::string& advertised,
                 const std::string& target, bool status, int64_t now, std::string& resolved) {
        for (auto& entry : entries_)
            if (!entry.address.empty() && now - entry.seen > kNameLeaseUs)
                entry = {};
        auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&](const Entry& entry) { return entry.address == address; });
        if (!advertised.empty()) {
            // Legacy fight telemetry uses a second advertisement labelled
            // CUBE-FIGHT. It may only alias an already confirmed target at
            // this exact address, never establish a new target association.
            if (advertised == "CUBE-FIGHT" && !target.empty() && found != entries_.end() &&
                found->name == target) {
                resolved = found->name;
                if (status)
                    found->seen = now;
                return true;
            }
            // An explicit different name revokes any cached matching name.
            if (!target.empty() && advertised != target) {
                if (found != entries_.end())
                    *found = {};
                return false;
            }
            if (found == entries_.end())
                found = std::min_element(
                    entries_.begin(), entries_.end(),
                    [](const Entry& a, const Entry& b) { return a.seen < b.seen; });
            *found = {address, advertised, now};
        }
        if (found != entries_.end() && !found->address.empty()) {
            resolved = found->name;
            // Valid status packets keep an established address association alive.
            if (status)
                found->seen = now;
            return target.empty() || resolved == target;
        }
        if (target.empty() && status) {
            resolved = "ZMK " + address;
            return true;
        }
        return false;
    }

private:
    struct Entry {
        std::string address, name;
        int64_t seen = -1;
    };
    std::array<Entry, 8> entries_{};
};
inline bool HasStatus(const uint8_t* data, size_t size) {
    if (!data)
        return false;
    for (size_t at = 0; at < size;) {
        const size_t length = data[at++];
        if (!length || length > size - at)
            return false;
        if (data[at] == 0xff) {
            KeyboardStatus keyboard;
            FightTelemetry fight;
            if (ParseKeyboardStatus(data + at + 1, length - 1, keyboard) ||
                ParseFightTelemetry(data + at + 1, length - 1, fight))
                return true;
        }
        at += length;
    }
    return false;
}
class Retry {
public:
    uint32_t FailureDelayMs() {
        if (attempts_ == 6)
            return 0;
        return std::min(1000u << attempts_++, 30000u);
    }
    void Healthy() { attempts_ = 0; }

private:
    unsigned attempts_ = 0;
};
}  // namespace scanner_policy

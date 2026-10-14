#pragma once
#include <algorithm>
#include "studio_protocol.h"
#include "velvet_positions.h"

namespace cube_dongle::studio {
inline Bytes CustomError(const std::string& reason) {
    Bytes body, result;
    Text(body, 1, reason);
    Message(result, 1, body);
    return result;
}
class Diagnostics {
public:
    void Sample(const SplitState::Snapshot& positions, uint32_t now) {
        const auto owners = VelvetOwnership();
        for (size_t p = 0; p < entries_.size(); ++p) {
            const bool down = positions[p / 8] & (1u << (p % 8));
            auto& entry = entries_[p];
            if (down == entry.down)
                continue;
            entry.source = (owners[0][p / 8] & (1u << (p % 8))) ? 1 : 2;
            if (down) {
                ++entry.presses;
                if (entry.releases) {
                    const uint32_t gap = now - entry.released_at;
                    entry.min_gap = std::min(entry.min_gap, gap);
                    for (size_t i = 0; i < 4; ++i)
                        if (gap < limits_[i])
                            ++entry.short_gaps[i];
                }
                entry.pressed_at = now;
            } else {
                ++entry.releases;
                entry.min_duration = std::min(entry.min_duration, now - entry.pressed_at);
                entry.released_at = now;
            }
            entry.down = down;
        }
    }
    Bytes Handle(const Bytes& payload, bool unlocked, uint32_t uptime) {
        Reader request(payload);
        const auto* op = request.Operation();
        if (!request.valid || !op || op->wire != 2)
            return CustomError("Malformed diagnostics request");
        Reader args(op->data, op->size);
        if (!args.valid || !args.Numbers({1, 2}))
            return CustomError("Malformed diagnostics arguments");
        Bytes result, body;
        if (op->tag == 1) {
            Number(body, 1, 1);
            Number(body, 2, 1);
            Number(body, 4, 0);  // Cube receives remote positions; no local GPIO scanner.
            Number(body, 5, 1);
            Number(body, 6, 61);
            Number(body, 7, uptime);
            Message(result, 2, body);
        } else if (op->tag == 2 && args.Value(1) == 0) {
            Text(body, 2, "Velvet BLE split positions");
            Number(body, 3, 1);
            Number(body, 4, 61);
            Number(body, 5, 61);
            Message(result, 3, body);
        } else if (op->tag == 5 && args.Value(1) == 0) {
            const auto offset = args.Value(2);
            if (offset > 61)
                return CustomError("Invalid position offset");
            Number(body, 1, 61);
            Number(body, 2, offset);
            for (size_t p = offset; p < std::min<size_t>(61, offset + 16); ++p)
                Number(body, 3, p);
            Message(result, 6, body);
        } else if (op->tag == 6) {
            const auto offset = args.Value(1);
            if (offset > 61)
                return CustomError("Invalid statistics offset");
            Number(body, 1, 61);
            Number(body, 2, offset);
            for (size_t p = offset; p < std::min<size_t>(61, offset + 16); ++p) {
                const auto& e = entries_[p];
                Bytes item;
                Number(item, 1, p);
                Number(item, 2, e.presses);
                Number(item, 3, e.releases);
                Number(item, 4, e.min_duration == UINT32_MAX ? 0 : e.min_duration);
                Number(item, 5, e.min_gap == UINT32_MAX ? 0 : e.min_gap);
                for (size_t i = 0; i < 4; ++i)
                    Number(item, 6 + i, e.short_gaps[i]);
                Number(item, 10, e.source);
                Message(body, 3, item);
            }
            Message(result, 7, body);
        } else if (op->tag == 7) {
            if (!unlocked)
                return CustomError("Studio is locked");
            // Keep current physical state, so reset never creates a phantom press.
            for (auto& e : entries_) {
                const bool down = e.down;
                e = Entry{};
                e.down = down;
                e.pressed_at = uptime;
            }
            Message(result, 8, body);
        } else
            return CustomError("No local scanner GPIO; peripheral relay not implemented");
        return result;
    }

private:
    struct Entry {
        uint32_t presses = 0, releases = 0, pressed_at = 0, released_at = 0;
        uint32_t min_duration = UINT32_MAX, min_gap = UINT32_MAX, source = 0;
        std::array<uint32_t, 4> short_gaps{};
        bool down = false;
    };
    std::array<Entry, 61> entries_{};
    const std::array<uint32_t, 4> limits_{{5, 10, 20, 50}};
};
}  // namespace cube_dongle::studio

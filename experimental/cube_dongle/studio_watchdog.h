#pragma once
#include "studio_diagnostics.h"

namespace cube_dongle::studio {
class WatchdogLog {
public:
    void Boot(uint32_t cause_bits) {
        ++boot_;
        if (entries_.size() == 16)
            entries_.erase(entries_.begin());
        entries_.push_back({boot_, cause_bits});
    }
    Bytes Export() const {
        Bytes bytes{'C', 'W', 'D', 1};
        auto word = [&bytes](uint32_t value) {
            for (unsigned i = 0; i < 4; ++i)
                bytes.push_back(value >> (i * 8));
        };
        word(boot_);
        bytes.push_back(entries_.size());
        for (const auto& entry : entries_) {
            word(entry.id);
            word(entry.cause);
        }
        word(Model::Hash(bytes.data(), bytes.size()));
        return bytes;
    }
    bool Load(const Bytes& bytes) {
        if (bytes.size() < 13 || std::memcmp(bytes.data(), "CWD\1", 4) || bytes[8] > 16 ||
            bytes.size() != 13 + size_t(bytes[8]) * 8)
            return false;
        auto word = [&bytes](size_t at) {
            uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i)
                value |= uint32_t(bytes[at + i]) << (i * 8);
            return value;
        };
        if (word(bytes.size() - 4) != Model::Hash(bytes.data(), bytes.size() - 4))
            return false;
        WatchdogLog next;
        next.boot_ = word(4);
        for (size_t at = 9; at < bytes.size() - 4; at += 8) {
            const auto id = word(at);
            if (!id || id > next.boot_ || (!next.entries_.empty() && id <= next.entries_.back().id))
                return false;
            next.entries_.push_back({id, word(at + 4)});
        }
        *this = std::move(next);
        return true;
    }
    Bytes Handle(const Bytes& payload, bool unlocked, const Rpc::Save& save) {
        Reader request(payload);
        const auto* op = request.Operation();
        if (!request.valid || !op || op->wire != 2)
            return CustomError("Invalid watchdog request");
        Reader args(op->data, op->size);
        if (!args.valid)
            return CustomError("Invalid watchdog arguments");
        Bytes body, result;
        if (op->tag == 1) {
            if (!args.Numbers({1}) || args.Value(1))
                return CustomError("Peripheral watchdog relay unavailable");
            Number(body, 1, 16);
            Number(body, 2, entries_.size());
            Message(result, 2, body);
        } else if (op->tag == 2) {
            if (!args.Numbers({1, 2}) || args.Value(2) || args.Value(1) > entries_.size())
                return CustomError("Invalid incident offset or source");
            const auto offset = args.Value(1);
            for (size_t i = offset; i < std::min<size_t>(offset + 8, entries_.size()); ++i) {
                Bytes incident, reset;
                Number(incident, 1, entries_[i].id);
                Number(incident, 3, 2);
                Number(incident, 4, entries_[i].id);
                Number(reset, 1, entries_[i].cause);
                Message(incident, 8, reset);
                Message(body, 1, incident);
            }
            Number(body, 2, entries_.size());
            Number(body, 3, offset);
            Message(result, 3, body);
        } else if (op->tag == 3) {
            if (!unlocked)
                return CustomError("Studio is locked");
            if (!args.Numbers({2, 3}) || args.Value(3))
                return CustomError("Peripheral watchdog relay unavailable");
            std::vector<uint32_t> ids;
            if (const auto* field = args.Get(1)) {
                if (field->wire == 0 && field->value <= UINT32_MAX)
                    ids.push_back(field->value);
                else if (field->wire == 2) {
                    size_t at = 0;
                    while (at < field->size) {
                        uint32_t id = 0;
                        bool complete = false;
                        for (unsigned shift = 0; shift <= 28 && at < field->size; shift += 7) {
                            const uint8_t b = field->data[at++];
                            if (shift == 28 && b > 15)
                                return CustomError("Invalid incident id");
                            id |= uint32_t(b & 127) << shift;
                            if (!(b & 128)) {
                                complete = true;
                                break;
                            }
                        }
                        if (!complete || ids.size() >= 16)
                            return CustomError("Invalid incident id list");
                        ids.push_back(id);
                    }
                } else
                    return CustomError("Invalid incident id list");
            }
            auto next = *this;
            next.entries_.erase(std::remove_if(next.entries_.begin(), next.entries_.end(),
                                               [&](const Entry& entry) {
                                                   return args.Value(2) ||
                                                          std::find(ids.begin(), ids.end(),
                                                                    entry.id) != ids.end();
                                               }),
                                next.entries_.end());
            const auto deleted = entries_.size() - next.entries_.size();
            if (!save(next.Export()))
                return CustomError("Watchdog log flash commit failed");
            *this = std::move(next);
            Number(body, 1, deleted);
            Message(result, 4, body);
        } else
            return CustomError("Dangerous watchdog test injection is disabled");
        return result;
    }

private:
    struct Entry {
        uint32_t id, cause;
    };
    std::vector<Entry> entries_;
    uint32_t boot_ = 0;
};
}  // namespace cube_dongle::studio

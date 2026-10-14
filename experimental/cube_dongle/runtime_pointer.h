#pragma once
#include <cmath>
#include "optional_input.h"
#include "studio_diagnostics.h"
namespace cube_dongle::studio {
struct ProcessorConfig {
    std::array<uint32_t, 30> values{};
    ProcessorConfig() {
        values[3] = values[4] = 1;
        values[8] = 50;
        values[9] = 200;
        values[12] = 10;
        values[13] = 100;
        values[18] = 16;
        values[19] = 1;
        values[20] = 100;
        values[21] = 20;
        values[26] = 20;
        values[27] = 100;
    }
    bool operator==(const ProcessorConfig& other) const { return values == other.values; }
    static bool Valid(unsigned tag, uint64_t value) {
        switch (tag) {
            case 3:
            case 4:
                return value >= 1 && value <= 1000;
            case 5:
                return int64_t(value) >= -360 && int64_t(value) <= 360;
            case 6:
            case 14:
            case 15:
            case 16:
            case 17:
            case 24:
            case 28:
                return value <= 1;
            case 7:
                return value < 4;
            case 8:
            case 9:
            case 13:
                return value <= 60000;
            case 10:
                return value <= 15;
            case 11:
                return value <= 2;
            case 12:
            case 19:
            case 26:
                return value >= 1 && value <= 32767;
            case 18:
                return value >= 2 && value <= 1000;
            case 20:
                return value >= 2 && value <= 1000;
            case 21:
                return value >= 1 && value <= 100;
            case 27:
                return value <= 800;
            case 29:
                return value <= 32767;
            default:
                return false;
        }
    }
    Bytes Encode(unsigned id, bool inertia = false) const {
        Bytes body;
        Number(body, 1, id);
        Text(body, 2, "Split Pointer " + std::to_string(id + 1));
        for (unsigned tag = 3; tag < 30; ++tag) {
            if (tag == 22 || tag == 23 || tag == 25)
                continue;
            Number(body, tag, tag == 5 ? uint64_t(int64_t(int32_t(values[tag]))) : values[tag]);
        }
        Number(body, 25, inertia);
        return body;
    }
};
struct PointerSettings {
    std::array<ProcessorConfig, 4> live{}, saved{};
    std::array<ProcessorConfig, 4> persisted{};
    Bytes Export() const {
        Bytes out{'C', 'P', 'S', 1};
        for (const auto& config : saved)
            for (auto value : config.values)
                for (unsigned i = 0; i < 4; ++i)
                    out.push_back(value >> (8 * i));
        const auto hash = Model::Hash(out.data(), out.size());
        for (unsigned i = 0; i < 4; ++i)
            out.push_back(hash >> (8 * i));
        return out;
    }
    bool Load(const Bytes& data) {
        if (data.size() != 488 || std::memcmp(data.data(), "CPS\1", 4))
            return false;
        uint32_t hash = 0;
        for (unsigned i = 0; i < 4; ++i)
            hash |= uint32_t(data[484 + i]) << (8 * i);
        if (hash != Model::Hash(data.data(), 484))
            return false;
        PointerSettings next;
        size_t at = 4;
        for (auto& config : next.saved)
            for (unsigned tag = 0; tag < 30; ++tag) {
                uint32_t value = 0;
                for (unsigned i = 0; i < 4; ++i)
                    value |= uint32_t(data[at++]) << (8 * i);
                if (tag >= 3 && tag != 22 && tag != 23 && tag != 25 &&
                    !ProcessorConfig::Valid(tag,
                                            tag == 5 ? uint64_t(int64_t(int32_t(value))) : value))
                    return false;
                config.values[tag] = value;
            }
        next.live = next.saved;
        next.persisted = next.saved;
        *this = next;
        return true;
    }
    using Notify = std::function<void(const Bytes&)>;
    Bytes Handle(const Bytes& payload, bool unlocked, uint8_t hardware, const Model& map,
                 const Rpc::Save& save, const Notify& notify, uint8_t inertia = 0) {
        Reader request(payload);
        const auto* op = request.Operation();
        if (!request.valid || !op || op->wire != 2)
            return CustomError("Invalid pointer request");
        Reader args(op->data, op->size);
        if (!args.valid || !args.Numbers({1, 2, 3}))
            return CustomError("Invalid pointer arguments");
        const auto id = args.Value(1);
        Bytes body, response;
        auto report = [&](unsigned index) {
            Bytes changed, event;
            Message(changed, 1, live[index].Encode(index, inertia & (1u << index)));
            Message(event, 1, changed);
            notify(event);
        };
        if (op->tag == 1) {
            for (unsigned i = 0; i < 4; ++i)
                if (hardware & (1u << i))
                    report(i);
            Message(response, 2, {});
            return response;
        }
        if (op->tag == 12) {
            for (size_t n = 0; n < map.count; ++n) {
                auto layer = map.order[n];
                Bytes item;
                Number(item, 1, layer);
                Text(item, 2, map.names[layer]);
                Message(body, 1, item);
            }
            Message(response, 13, body);
            return response;
        }
        if (op->tag >= 20 && op->tag <= 22) {
            if (!unlocked)
                return CustomError("Studio is locked");
            auto next = *this;
            if (op->tag == 21) {
                next.live = next.saved = persisted;
            } else if (op->tag == 22) {
                next = PointerSettings{};
            }
            if (op->tag != 21 && !save(next.Export()))
                return CustomError("Pointer flash commit failed");
            if (op->tag != 21)
                next.persisted = next.saved;
            *this = next;
            for (unsigned i = 0; i < 4; ++i)
                if (hardware & (1u << i))
                    report(i);
            Message(response, op->tag + 1, {});
            return response;
        }
        if (id >= 4 || !(hardware & (1u << id)))
            return CustomError("Pointer hardware not installed");
        if (op->tag == 2) {
            Message(body, 1, live[id].Encode(id, inertia & (1u << id)));
            Message(response, 3, body);
            return response;
        }
        if (!unlocked)
            return CustomError("Studio is locked");
        const std::array<unsigned, 34> tags{{0,  0,  0,  3,  4,  5,  0,  6,  7,  8, 9, 10,
                                             0,  11, 12, 13, 14, 15, 16, 17, 0,  0, 0, 18,
                                             19, 20, 21, 0,  0,  24, 26, 27, 28, 29}};
        if (op->tag >= tags.size() || (op->tag != 6 && !tags[op->tag]))
            return CustomError("Unknown pointer operation");
        auto next = *this;
        const auto tag = tags[op->tag];
        const auto value = args.Value(2);
        const auto mode = op->tag == 29 ? 2 : args.Value(3);
        if (mode > 2 || (op->tag != 6 && !ProcessorConfig::Valid(tag, value)) ||
            (tag == 7 && !map.Active(value)))
            return CustomError("Invalid pointer setting");
        if (op->tag == 6)
            next.live[id] = next.saved[id] = ProcessorConfig{};
        else {
            next.live[id].values[tag] = uint32_t(value);
            if (mode != 2)
                next.saved[id].values[tag] = uint32_t(value);
        }
        if (mode == 0 && !save(next.Export()))
            return CustomError("Pointer flash commit failed");
        if (mode == 0)
            next.persisted = next.saved;
        *this = next;
        report(id);
        Message(response, op->tag + 1, {});
        return response;
    }
};
class PointerEngine {
public:
    void Register(unsigned id, bool present) {
        if (id < 4) {
            state_[id] = {};
            registered_[id] = present;
        }
    }
    void Configure(const std::array<ProcessorConfig, 4>& config) {
        for (unsigned i = 0; i < 4; ++i)
            if (!(config_[i] == config[i])) {
                const uint8_t buttons = state_[i].buttons;
                const bool was_active = state_[i].inertia;
                state_[i] = {};
                state_[i].buttons = buttons;
                state_[i].stop_reason = was_active ? 5 : 0;
                config_[i] = config[i];
            }
    }
    uint8_t Buttons() const {
        uint8_t bits = 0;
        for (const auto& s : state_)
            bits |= s.buttons;
        return bits;
    }
    bool Input(unsigned id, const uint8_t* bytes, size_t size, uint32_t now, size_t layer,
               PointerReport& report) {
        if (id >= 4 || !registered_[id] || !bytes || size != 8 || bytes[7] > 1)
            return false;
        const uint16_t code = bytes[1] | (uint16_t(bytes[2]) << 8);
        const uint32_t raw = uint32_t(bytes[3]) | (uint32_t(bytes[4]) << 8) |
                             (uint32_t(bytes[5]) << 16) | (uint32_t(bytes[6]) << 24);
        const int64_t value = int32_t(raw);
        auto& s = state_[id];
        if (bytes[0] == 1 && code >= 0x110 && code <= 0x114 && value >= 0 && value <= 1) {
            const uint8_t bit = 1u << (code - 0x110);
            s.buttons = value ? s.buttons | bit : s.buttons & ~bit;
        } else if (bytes[0] == 2) {
            if (code == 0)
                s.x = Clamp(s.x + value);
            else if (code == 1)
                s.y = Clamp(s.y + value);
            else if (code == 8)
                s.wheel = Clamp(s.wheel + value);
            else if (code == 6)
                s.pan = Clamp(s.pan + value);
            else
                return false;
        } else
            return false;
        if (!bytes[7])
            return false;
        report = {};
        report.buttons = Buttons();
        const auto& c = config_[id].values;
        if (c[10] && (layer >= 4 || !(c[10] & (1u << layer)))) {
            s.x = s.y = s.wheel = s.pan = 0;
            s.inertia = false;
            return true;
        }
        double x = double(s.x) * c[3] / c[4], y = double(s.y) * c[3] / c[4];
        if (c[15])
            std::swap(x, y);
        if (c[16])
            x = -x;
        if (c[17])
            y = -y;
        const double angle = int32_t(c[5]) * 3.141592653589793 / 180.0;
        const double dx = x * std::cos(angle) - y * std::sin(angle),
                     dy = x * std::sin(angle) + y * std::cos(angle);
        x = dx;
        y = dy;
        if (c[11]) {
            if (uint32_t(now - s.snap_at) > c[13]) {
                s.snap_at = now;
                s.unsnapped = false;
                s.off_axis = 0;
            }
            s.off_axis += std::abs(c[11] == 1 ? y : x);
            if (s.off_axis >= c[12])
                s.unsnapped = true;
            if (!s.unsnapped) {
                if (c[11] == 1)
                    y = 0;
                else
                    x = 0;
            }
        }
        report.x = Rounded(x);
        report.y = Rounded(y);
        report.wheel = int8_t(std::clamp<int64_t>(s.wheel, -128, 127));
        report.pan = int8_t(std::clamp<int64_t>(s.pan, -128, 127));
        if (c[14]) {
            report.wheel = int8_t(std::clamp<int>(report.wheel + report.y, -128, 127));
            report.pan = int8_t(std::clamp<int>(report.pan + report.x, -128, 127));
            report.x = report.y = 0;
        }
        s.x = s.y = s.wheel = s.pan = 0;
        const int32_t vx = c[14] ? report.pan : report.x, vy = c[14] ? report.wheel : report.y;
        if (vx || vy) {
            if (!s.moving) {
                s.moving = true;
                s.started = now;
            }
            s.last = now;
            if (uint32_t(now - s.history_at) > c[20] || (vx && s.vx && ((vx > 0) != (s.vx > 0))) ||
                (vy && s.vy && ((vy > 0) != (s.vy > 0)))) {
                s.history_x = s.history_y = 0;
                s.history_at = now;
            }
            s.history_x = Clamp(s.history_x + vx);
            s.history_y = Clamp(s.history_y + vy);
            s.fast = uint64_t(std::abs(s.history_x) + std::abs(s.history_y)) >= c[26];
            s.vx = vx;
            s.vy = vy;
            s.next = now + c[18];
            s.inertia = c[28] && (uint64_t(std::abs(vx) + std::abs(vy)) >= c[19]);
        }
        return true;
    }
    std::vector<PointerReport> Tick(uint32_t now, size_t layer) {
        std::vector<PointerReport> reports;
        for (unsigned i = 0; i < 4; ++i) {
            auto& s = state_[i];
            const auto& c = config_[i].values;
            if (!registered_[i])
                continue;
            if (c[10] && (layer >= 4 || !(c[10] & (1u << layer)))) {
                s.inertia = false;
                s.stop_reason = 4;
            }
            if (!s.inertia || int32_t(now - s.next) < 0)
                continue;
            s.next = now + c[18];
            s.vx = s.vx * int32_t(100 - c[21]) / 100;
            s.vy = s.vy * int32_t(100 - c[21]) / 100;
            if (uint64_t(std::abs(s.vx) + std::abs(s.vy)) < c[19]) {
                s.inertia = false;
                s.stop_reason = 1;
                continue;
            }
            int64_t x = s.vx, y = s.vy;
            if (s.fast) {
                x = x * c[27] / 100;
                y = y * c[27] / 100;
            } else if (c[29]) {
                x = std::clamp<int64_t>(x, -int64_t(c[29]), c[29]);
                y = std::clamp<int64_t>(y, -int64_t(c[29]), c[29]);
            }
            PointerReport report;
            report.buttons = Buttons();
            if (c[14]) {
                report.pan = int8_t(std::clamp<int64_t>(x, -128, 127));
                report.wheel = int8_t(std::clamp<int64_t>(y, -128, 127));
            } else {
                report.x = Clamp(x);
                report.y = Clamp(y);
            }
            reports.push_back(report);
        }
        return reports;
    }
    uint8_t Layers(uint32_t now) {
        uint8_t mask = 0;
        for (unsigned i = 0; i < 4; ++i) {
            auto& s = state_[i];
            const auto& c = config_[i].values;
            if (s.moving && uint32_t(now - s.last) > c[9])
                s.moving = false;
            if (registered_[i] && c[6] && s.moving && uint32_t(now - s.started) >= c[8])
                mask |= 1u << c[7];
        }
        return mask;
    }
    void CancelMotion() {
        for (auto& s : state_) {
            s.x = s.y = s.wheel = s.pan = 0;
            s.inertia = s.moving = false;
        }
    }
    bool NeedsTick() const {
        for (const auto& state : state_)
            if (state.moving || state.inertia)
                return true;
        return false;
    }
    uint8_t Inertia() const {
        uint8_t mask = 0;
        for (unsigned i = 0; i < 4; ++i)
            if (state_[i].inertia)
                mask |= 1u << i;
        return mask;
    }

    uint8_t Fast() const {
        uint8_t mask = 0;
        for (unsigned i = 0; i < 4; ++i)
            if (state_[i].fast)
                mask |= 1u << i;
        return mask;
    }
    unsigned StopReason(unsigned index) const { return index < 4 ? state_[index].stop_reason : 0; }

private:
    static int32_t Clamp(int64_t value) {
        return int32_t(std::clamp<int64_t>(value, -32767, 32767));
    }
    static int16_t Rounded(double value) {
        return int16_t(std::round(std::clamp(value, -32767.0, 32767.0)));
    }
    struct State {
        int64_t x = 0, y = 0, wheel = 0, pan = 0;
        int32_t vx = 0, vy = 0, history_x = 0, history_y = 0;
        uint8_t buttons = 0;
        uint8_t stop_reason = 0;
        uint32_t next = 0, last = 0, started = 0, snap_at = 0, history_at = 0;
        bool inertia = false, moving = false, unsnapped = false, fast = false;
        double off_axis = 0;
    };
    std::array<State, 4> state_{};
    std::array<bool, 4> registered_{};
    std::array<ProcessorConfig, 4> config_{};
};
}  // namespace cube_dongle::studio

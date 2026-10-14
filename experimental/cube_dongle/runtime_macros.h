#pragma once
#include "report_queue.h"
#include "studio_diagnostics.h"

namespace cube_dongle::studio {
struct Macro {
    std::string name;
    std::vector<Bytes> steps;
};
struct MacroModel {
    std::array<Macro, 8> entries;
    uint32_t tap_ms = 20;
    Bytes Export() const {
        Bytes out{'C', 'M', 'R', 1};
        auto word = [&out](unsigned value) {
            out.push_back(value & 255);
            out.push_back(value >> 8);
        };
        word(tap_ms);
        for (const auto& macro : entries) {
            out.push_back(macro.name.size());
            out.insert(out.end(), macro.name.begin(), macro.name.end());
            out.push_back(macro.steps.size());
            for (const auto& step : macro.steps) {
                word(step.size());
                out.insert(out.end(), step.begin(), step.end());
            }
        }
        const uint32_t hash = Model::Hash(out.data(), out.size());
        for (unsigned i = 0; i < 4; ++i)
            out.push_back(hash >> (8 * i));
        return out;
    }
    static bool Name(const std::string& value) {
        return !value.empty() && value.size() <= 31 && value.find('\0') == std::string::npos;
    }
    static bool Step(const Bytes& bytes) {
        if (bytes.size() > 128)
            return false;
        Reader reader(bytes);
        const auto* op = reader.Operation();
        if (!reader.valid || !op || op->wire != 2)
            return false;
        Reader data(op->data, op->size);
        if (!data.valid)
            return false;
        if (op->tag == 5) {
            const auto* keys = data.Get(1);
            if (!keys || keys->wire != 2 || !keys->size || keys->size > 64)
                return false;
            for (size_t i = 0; i < keys->size; ++i)
                if ((keys->data[i] & 127) < 4 || (keys->data[i] & 127) > 0x38)
                    return false;
            return true;
        }
        if (!data.Numbers({1, 2, 3}))
            return false;
        if (op->tag == 4)
            return data.Value(1) <= 60000;
        Binding binding;
        return op->tag >= 1 && op->tag <= 3 && data.Value(1) == kKp &&
               BindingFromWire(data.Value(1) * 2, data.Value(2), data.Value(3), binding) &&
               (binding.kind == Kind::Key || binding.kind == Kind::Media);
    }
    bool Load(const Bytes& bytes) {
        if (bytes.size() < 18 || bytes.size() > 3072 || std::memcmp(bytes.data(), "CMR\1", 4))
            return false;
        uint32_t hash = 0;
        for (unsigned i = 0; i < 4; ++i)
            hash |= uint32_t(bytes[bytes.size() - 4 + i]) << (8 * i);
        if (hash != Model::Hash(bytes.data(), bytes.size() - 4))
            return false;
        MacroModel next;
        size_t at = 4, end = bytes.size() - 4;
        next.tap_ms = bytes[at] | (unsigned(bytes[at + 1]) << 8);
        at += 2;
        if (next.tap_ms < 5 || next.tap_ms > 5000)
            return false;
        for (size_t slot = 0; slot < next.entries.size(); ++slot) {
            auto& macro = next.entries[slot];
            if (at >= end)
                return false;
            const auto length = bytes[at++];
            if (length > 31 || length > end - at)
                return false;
            macro.name.assign(reinterpret_cast<const char*>(bytes.data() + at), length);
            at += length;
            if (length && !Name(macro.name))
                return false;
            for (size_t old = 0; old < slot; ++old)
                if (length && next.entries[old].name == macro.name)
                    return false;
            if (at >= end)
                return false;
            unsigned count = bytes[at++];
            if (count > 64 || (!length && count))
                return false;
            while (count--) {
                if (end - at < 2)
                    return false;
                size_t size = bytes[at] | (unsigned(bytes[at + 1]) << 8);
                at += 2;
                if (size > 128 || size > end - at)
                    return false;
                Bytes step(bytes.begin() + at, bytes.begin() + at + size);
                if (!Step(step))
                    return false;
                macro.steps.push_back(std::move(step));
                at += size;
            }
        }
        if (at != end)
            return false;
        *this = std::move(next);
        return true;
    }
};
class RuntimeMacros {
public:
    MacroModel model;
    RuntimeMacros() : saved_(model.Export()) {}
    void Load(const Bytes& bytes) {
        if (model.Load(bytes))
            saved_ = bytes;
    }
    Bytes Handle(const Bytes& payload, bool unlocked, const Rpc::Save& save) {
        Reader request(payload);
        const auto* op = request.Operation();
        if (!request.valid || !op || op->wire != 2)
            return CustomError("Invalid macro request");
        Reader args(op->data, op->size);
        if (!args.valid)
            return CustomError("Invalid macro arguments");
        Bytes body, result;
        if (op->tag == 1) {
            for (size_t slot = 0; slot < model.entries.size(); ++slot) {
                const auto& macro = model.entries[slot];
                if (macro.name.empty())
                    continue;
                Bytes item;
                Number(item, 1, slot);
                Text(item, 2, macro.name);
                size_t size = 0;
                for (const auto& step : macro.steps)
                    size += step.size();
                Number(item, 3, size);
                Number(item, 4, model.Export() != saved_);
                Message(body, 1, item);
            }
            Number(body, 2, 3072);
            Number(body, 3, 31);
            Message(result, 3, body);
            return result;
        }
        if (op->tag == 2) {
            if (!unlocked)
                return CustomError("Studio is locked");
            if (!args.Numbers({1}) || args.Value(1) >= 8 ||
                model.entries[args.Value(1)].name.empty())
                return CustomError("Unknown macro slot");
            const auto& macro = model.entries[args.Value(1)];
            Bytes detail;
            Number(detail, 1, args.Value(1));
            Text(detail, 2, macro.name);
            size_t size = 0;
            for (const auto& step : macro.steps) {
                Message(detail, 3, step);
                size += step.size();
            }
            Number(detail, 4, size);
            Message(body, 1, detail);
            Message(result, 4, body);
            return result;
        }
        if (op->tag == 5) {
            Bytes settings;
            Number(settings, 1, model.tap_ms);
            Number(settings, 2, 8);
            Number(settings, 3, kKp);
            Number(settings, 4, 3072);
            Number(settings, 5, model.Export().size());
            Message(body, 1, settings);
            Message(result, 5, body);
            return result;
        }
        if (!unlocked)
            return CustomError("Studio is locked");
        auto next = model;
        bool persist = false;
        auto text = [&args](unsigned field) {
            const auto* f = args.Get(field);
            return f && f->wire == 2 ? std::string(reinterpret_cast<const char*>(f->data), f->size)
                                     : std::string{};
        };
        auto find = [&next](const std::string& name) {
            for (size_t i = 0; i < next.entries.size(); ++i)
                if (!name.empty() && next.entries[i].name == name)
                    return i;
            return next.entries.size();
        };
        if (op->tag == 12) {
            const auto name = text(1);
            if (!MacroModel::Name(name) || find(name) < 8 || !args.Numbers({2}))
                return CustomError("Invalid or duplicate macro name");
            size_t slot = 0;
            while (slot < 8 && !next.entries[slot].name.empty())
                ++slot;
            if (slot == 8)
                return CustomError("Macro slots exhausted");
            next.entries[slot].name = name;
            persist = args.Value(2);
        } else if (op->tag == 13 || op->tag == 14) {
            const auto slot = find(text(1));
            if (slot >= 8)
                return CustomError("Unknown macro name");
            if (op->tag == 13) {
                next.entries[slot] = {};
                persist = true;
            } else {
                const auto name = text(2);
                if (!MacroModel::Name(name) || find(name) < 8 || !args.Numbers({3}))
                    return CustomError("Invalid or duplicate macro name");
                next.entries[slot].name = name;
                persist = args.Value(3);
            }
        } else if (op->tag == 6) {
            if (!args.Numbers({1, 2}) || args.Value(1) < 5 || args.Value(1) > 5000)
                return CustomError("Tap duration outside 5..5000 ms");
            next.tap_ms = args.Value(1);
            persist = args.Value(2);
        } else if (op->tag == 9) {
            persist = true;
        } else if (op->tag == 10) {
            if (!next.Load(saved_))
                return CustomError("Saved macro state is corrupt");
        } else if (op->tag == 4 || op->tag == 7 || op->tag == 11 || op->tag == 15) {
            if (!args.Numbers({1}) || args.Value(1) >= 8 ||
                next.entries[args.Value(1)].name.empty())
                return CustomError("Unknown macro slot");
            auto& macro = next.entries[args.Value(1)];
            if (op->tag == 4) {
                if (!args.Numbers({2, 3}) || args.Value(2) > 64)
                    return CustomError("Macro step count exceeds 64");
                Bytes zero_delay;
                Message(zero_delay, 4, {});
                macro.steps.resize(args.Value(2), zero_delay);
                persist = args.Value(3);
            } else if (op->tag == 15) {
                if (!args.Numbers({2}))
                    return CustomError("Invalid reset request");
                macro = {};
                persist = args.Value(2);
            } else {
                const auto* step = args.Get(op->tag == 7 ? 3 : 2);
                if (!step || step->wire != 2)
                    return CustomError("Missing macro step");
                Bytes value(step->data, step->data + step->size);
                if (!MacroModel::Step(value))
                    return CustomError("Unsupported or malformed macro step");
                if (op->tag == 7) {
                    if (!args.Numbers({2, 4}) || args.Value(2) >= macro.steps.size())
                        return CustomError("Invalid macro step index");
                    macro.steps[args.Value(2)] = std::move(value);
                    persist = args.Value(4);
                } else {
                    if (!args.Numbers({3}) || macro.steps.size() >= 64)
                        return CustomError("Macro step limit exceeded");
                    macro.steps.push_back(std::move(value));
                    persist = args.Value(3);
                }
            }
        } else
            return CustomError("Unsupported macro operation");
        const auto bytes = next.Export();
        if (bytes.size() > 3072)
            return CustomError("Macro pool exhausted");
        if (persist && !save(bytes))
            return CustomError("Macro flash commit failed; previous state retained");
        model = std::move(next);
        if (persist)
            saved_ = bytes;
        Number(body, 1, 1);
        Text(body, 2, "OK");
        Message(result, 2, body);
        return result;
    }

private:
    Bytes saved_;
};
class MacroPlayer {
public:
    bool Start(const Macro& macro, uint32_t tap_ms, uint32_t now) {
        if (active_ || macro.name.empty() || macro.steps.empty())
            return false;
        if (macro.steps.size() > 64 || tap_ms < 5 || tap_ms > 5000)
            return false;
        for (const auto& step : macro.steps)
            if (!MacroModel::Step(step))
                return false;
        macro_ = macro;
        tap_ms_ = tap_ms;
        active_ = true;
        began_ = deadline_ = now;
        at_ = 0;
        return true;
    }
    void Cancel() {
        active_ = false;
        tap_ = {};
        held_.fill({});
        macro_ = {};
        sequence_.clear();
        sequence_at_ = 0;
    }
    bool Active() const { return active_; }
    void Tick(uint32_t now) {
        if (!active_ || int32_t(now - deadline_) < 0)
            return;
        if (uint32_t(now - began_) > 600000) {
            Cancel();
            return;
        }
        if (tap_.kind != Kind::None) {
            tap_ = {};
            deadline_ = now + 2;
            return;
        }
        if (sequence_at_ < sequence_.size()) {
            SequenceTap(now);
            return;
        }
        if (at_ >= macro_.steps.size()) {
            Cancel();
            return;
        }
        Reader step(macro_.steps[at_++]);
        const auto* op = step.Operation();
        if (!step.valid || !op) {
            Cancel();
            return;
        }
        Reader data(op->data, op->size);
        deadline_ = now + 2;
        if (op->tag == 5) {
            const auto* keys = data.Get(1);
            sequence_.assign(keys->data, keys->data + keys->size);
            sequence_at_ = 0;
            SequenceTap(now);
            return;
        }
        if (op->tag == 4) {
            deadline_ = now + std::max<uint32_t>(2, data.Value(1));
            return;
        }
        Binding binding;
        if (!BindingFromWire(data.Value(1) * 2, data.Value(2), data.Value(3), binding)) {
            Cancel();
            return;
        }
        if (op->tag == 3) {
            tap_ = binding;
            deadline_ = now + tap_ms_;
        } else if (op->tag == 1) {
            for (auto& held : held_)
                if (held.kind == Kind::None || Equal(held, binding)) {
                    held = binding;
                    return;
                }
            Cancel();
        } else if (op->tag == 2) {
            for (auto& held : held_)
                if (Equal(held, binding))
                    held = {};
        }
    }
    ReportFrame Merge(ReportFrame physical) const {
        std::array<bool, 256> usages{};
        for (size_t i = 2; i < physical.keyboard.size(); ++i)
            usages[physical.keyboard[i]] = true;
        auto add = [&](Binding binding) {
            if (binding.kind == Kind::Media)
                physical.media |= binding.value & 7;
            if (binding.kind != Kind::Key)
                return;
            physical.keyboard[0] |= binding.modifiers;
            if (binding.value >= 0xe0 && binding.value <= 0xe7)
                physical.keyboard[0] |= 1u << (binding.value - 0xe0);
            else if (binding.value < 0xe0)
                usages[binding.value] = true;
        };
        add(tap_);
        for (const auto& held : held_)
            add(held);
        size_t count = 0;
        physical.keyboard[1] = 0;
        for (size_t i = 2; i < 8; ++i)
            physical.keyboard[i] = 0;
        for (size_t usage = 4; usage < 0xe0; ++usage)
            if (usages[usage]) {
                if (count < 6)
                    physical.keyboard[2 + count] = usage;
                ++count;
            }
        if (count > 6 || usages[1])
            for (size_t i = 2; i < 8; ++i)
                physical.keyboard[i] = 1;
        return physical;
    }

private:
    void SequenceTap(uint32_t now) {
        const uint8_t key = sequence_[sequence_at_++];
        tap_ = {Kind::Key, uint16_t(key & 127), uint8_t((key & 128) ? 2 : 0)};
        deadline_ = now + tap_ms_;
    }
    static bool Equal(Binding a, Binding b) {
        return a.kind == b.kind && a.value == b.value && a.modifiers == b.modifiers;
    }
    Macro macro_;
    Bytes sequence_;
    size_t sequence_at_ = 0;
    std::array<Binding, 16> held_{};
    Binding tap_;
    size_t at_ = 0;
    uint32_t tap_ms_ = 20, began_ = 0, deadline_ = 0;
    bool active_ = false;
};
}  // namespace cube_dongle::studio

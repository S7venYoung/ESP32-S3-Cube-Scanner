#pragma once
#include "report_queue.h"
#include "studio_diagnostics.h"

namespace cube_dongle::studio {
struct Combo {
    std::string name;
    std::vector<uint8_t> keys;
    Binding binding;
    uint8_t layers = 0, slow_override = 0;
    bool enabled = false;
    uint16_t timeout = 0;
    uint32_t prior_idle = 0;
};
struct ComboModel {
    std::array<Combo, 8> entries;
    uint32_t timeout = 40, prior_idle = 0;
    bool slow_release = false;
    static bool Valid(const Combo& combo) {
        if (combo.binding.kind == Kind::ModTap || combo.binding.kind == Kind::LayerTap ||
            combo.binding.kind == Kind::RuntimeTapDance)
            return false;  // Hold-tap belongs to physical key positions, not a synthetic chord.
        if (combo.name.size() > 31 || (combo.keys.size() && combo.keys.size() < 2) ||
            combo.keys.size() > 4 || combo.layers > 15 || combo.timeout > 1000 ||
            combo.prior_idle > 60000 || combo.slow_override > 2 ||
            (combo.enabled && combo.keys.empty()))
            return false;
        for (char c : combo.name)
            if (uint8_t(c) < 32 || uint8_t(c) == 127)
                return false;
        std::array<bool, 61> seen{};
        for (auto key : combo.keys) {
            if (key >= 61 || seen[key])
                return false;
            seen[key] = true;
        }
        Binding checked;
        return BindingFromWire(Behavior(combo.binding) * 2, Param1(combo.binding),
                               Param2(combo.binding), checked) &&
               checked.kind == combo.binding.kind && checked.value == combo.binding.value &&
               checked.modifiers == combo.binding.modifiers;
    }
    Bytes Export() const {
        Bytes out{'C', 'C', 'B', 1};
        auto word = [&out](uint32_t value, size_t size) {
            for (size_t i = 0; i < size; ++i)
                out.push_back(value >> (i * 8));
        };
        word(timeout, 4);
        word(prior_idle, 4);
        out.push_back(slow_release);
        for (const auto& combo : entries) {
            out.push_back(combo.name.size());
            out.insert(out.end(), combo.name.begin(), combo.name.end());
            out.push_back(combo.keys.size());
            out.insert(out.end(), combo.keys.begin(), combo.keys.end());
            out.push_back(uint8_t(combo.binding.kind));
            word(combo.binding.value, 2);
            out.push_back(combo.binding.modifiers);
            out.push_back(combo.layers);
            out.push_back(combo.enabled);
            word(combo.timeout, 2);
            word(combo.prior_idle, 4);
            out.push_back(combo.slow_override);
        }
        word(Model::Hash(out.data(), out.size()), 4);
        return out;
    }
    bool Load(const Bytes& bytes) {
        if (bytes.size() < 137 || bytes.size() > 1024 || std::memcmp(bytes.data(), "CCB\1", 4))
            return false;
        size_t at = bytes.size() - 4;
        auto word = [&bytes, &at](size_t size) {
            uint32_t value = 0;
            for (size_t i = 0; i < size; ++i)
                value |= uint32_t(bytes[at++]) << (i * 8);
            return value;
        };
        if (word(4) != Model::Hash(bytes.data(), bytes.size() - 4))
            return false;
        at = 4;
        const auto end = bytes.size() - 4;
        ComboModel next;
        next.timeout = word(4);
        next.prior_idle = word(4);
        if (bytes[at] > 1 || !next.timeout || next.timeout > 1000 || next.prior_idle > 60000)
            return false;
        next.slow_release = bytes[at++];
        for (auto& combo : next.entries) {
            if (at >= end)
                return false;
            const size_t name_size = bytes[at++];
            if (name_size > 31 || name_size >= end - at)
                return false;
            combo.name.assign(reinterpret_cast<const char*>(bytes.data() + at), name_size);
            at += name_size;
            const size_t count = bytes[at++];
            if (count > 4 || end - at < count + 13)
                return false;
            combo.keys.assign(bytes.begin() + at, bytes.begin() + at + count);
            at += count;
            const auto kind = bytes[at++];
            if (kind > unsigned(Kind::RuntimeMacro))
                return false;
            combo.binding.kind = Kind(kind);
            combo.binding.value = word(2);
            combo.binding.modifiers = bytes[at++];
            combo.layers = bytes[at++];
            if (bytes[at] > 1)
                return false;
            combo.enabled = bytes[at++];
            combo.timeout = word(2);
            combo.prior_idle = word(4);
            combo.slow_override = bytes[at++];
            if (!Valid(combo))
                return false;
        }
        if (at != end)
            return false;
        *this = std::move(next);
        return true;
    }
    Bytes Encode(size_t index) const {
        const auto& combo = entries[index];
        Bytes out, binding;
        Number(out, 1, index);
        Text(out, 2, combo.name);
        for (auto key : combo.keys)
            Number(out, 3, key);
        Number(binding, 1, Behavior(combo.binding));
        Number(binding, 2, Param1(combo.binding));
        Number(binding, 3, Param2(combo.binding));
        Message(out, 4, binding);
        Number(out, 6, combo.layers);
        Number(out, 8, combo.enabled);
        Number(out, 9, combo.timeout);
        Number(out, 10, combo.prior_idle);
        Number(out, 11, combo.slow_override);
        Number(out, 12, combo.keys.empty() ? 0 : 3);
        return out;
    }
};
class RuntimeCombos {
public:
    ComboModel model;
    RuntimeCombos() : saved_(model.Export()) {}
    void Load(const Bytes& bytes) {
        if (model.Load(bytes))
            saved_ = bytes;
    }
    Bytes Handle(const Bytes& payload, bool unlocked, const Rpc::Save& save) {
        Reader request(payload);
        const auto* op = request.Operation();
        if (!request.valid || !op || op->wire != 2)
            return CustomError("Malformed combo request");
        Reader args(
            op->data, op->size,
            op->tag == 3 ? std::initializer_list<uint32_t>{2} : std::initializer_list<uint32_t>{});
        if (!args.valid)
            return CustomError("Malformed combo arguments");
        Bytes body, response;
        if (op->tag == 1) {
            for (size_t i = 0; i < model.entries.size(); ++i)
                Message(body, 1, model.Encode(i));
            Message(response, 2, body);
            return response;
        }
        if (op->tag == 2) {
            if (!args.Numbers({1}) || args.Value(1) >= 8)
                return CustomError("Invalid combo index");
            Message(body, 1, model.Encode(args.Value(1)));
            Message(response, 3, body);
            return response;
        }
        if (op->tag == 6) {
            Bytes settings;
            Number(settings, 1, model.timeout);
            Number(settings, 2, model.slow_release);
            Number(settings, 3, 8);
            Number(settings, 4, model.prior_idle);
            Message(body, 1, settings);
            Message(response, 5, body);
            return response;
        }
        if (!unlocked)
            return CustomError("Studio is locked");
        auto next = model;
        bool persist = false;
        if (op->tag == 3) {
            if (!args.Numbers({1, 5, 7, 8, 9, 10, 11}) || args.Value(1) >= 8 ||
                args.Value(5) > 15 || args.Value(7) > 1 || args.Value(8) > 1 ||
                args.Value(9) > 1000 || args.Value(10) > 60000 || args.Value(11) > 2)
                return CustomError("Invalid combo settings");
            auto& combo = next.entries[args.Value(1)];
            combo.keys.clear();
            for (const auto& field : args.Repeated(2)) {
                if (field.wire == 0) {
                    if (field.value >= 61 || combo.keys.size() == 4)
                        return CustomError("Invalid combo position");
                    combo.keys.push_back(field.value);
                } else if (field.wire == 2) {
                    // All valid Cube positions fit in one canonical varint byte.
                    for (size_t i = 0; i < field.size; ++i) {
                        if (field.data[i] >= 61 || combo.keys.size() == 4)
                            return CustomError("Invalid packed combo positions");
                        combo.keys.push_back(field.data[i]);
                    }
                } else
                    return CustomError("Invalid combo position encoding");
            }
            const auto* binding = args.Get(3);
            if (!binding || binding->wire != 2 || combo.keys.size() < 2)
                return CustomError("A combo needs a behavior and 2..4 positions");
            Reader value(binding->data, binding->size);
            if (!value.valid || !value.Numbers({1, 2, 3}) || value.Value(1) > UINT32_MAX ||
                !BindingFromWire(value.Value(1) * 2, value.Value(2), value.Value(3), combo.binding))
                return CustomError("Invalid combo behavior");
            combo.layers = args.Value(5);
            combo.enabled = args.Value(7);
            combo.timeout = args.Value(9);
            combo.prior_idle = args.Value(10);
            combo.slow_override = args.Value(11);
            persist = args.Value(8);
            if (!ComboModel::Valid(combo))
                return CustomError("Invalid or duplicate combo positions");
        } else if (op->tag == 4 || op->tag == 5 || op->tag == 12) {
            if (!args.Numbers({1}) || args.Value(1) >= 8)
                return CustomError("Invalid combo index");
            auto& combo = next.entries[args.Value(1)];
            if (op->tag == 4) {
                const auto* name = args.Get(2);
                if (!name || name->wire != 2 || name->size > 31 || !args.Numbers({3}))
                    return CustomError("Invalid combo name");
                combo.name.assign(reinterpret_cast<const char*>(name->data), name->size);
                persist = args.Value(3);
                if (!ComboModel::Valid(combo))
                    return CustomError("Invalid combo name");
            } else {
                if (!args.Numbers({2}))
                    return CustomError("Invalid delete arguments");
                combo = {};
                persist = op->tag == 12 || args.Value(2);
            }
        } else if (op->tag == 7 || op->tag == 8 || op->tag == 11) {
            if (!args.Numbers({1, 2}) || args.Value(2) > 1)
                return CustomError("Invalid global combo settings");
            const auto value = args.Value(1);
            if (op->tag == 7) {
                if (!value || value > 1000)
                    return CustomError("Timeout outside 1..1000 ms");
                next.timeout = value;
            } else if (op->tag == 8) {
                if (value > 1)
                    return CustomError("Invalid slow-release value");
                next.slow_release = value;
            } else {
                if (value > 60000)
                    return CustomError("Prior idle exceeds 60 seconds");
                next.prior_idle = value;
            }
            persist = args.Value(2);
        } else if (op->tag == 9)
            persist = true;
        else if (op->tag == 10) {
            if (!next.Load(saved_))
                return CustomError("Saved combo state is corrupt");
        } else
            return CustomError("Unsupported combo operation");
        auto bytes = next.Export();
        if (persist && !save(bytes))
            return CustomError("Combo flash commit failed; previous state retained");
        model = std::move(next);
        if (persist)
            saved_ = bytes;
        Number(body, 1, 1);
        Text(body, 2, "OK");
        Message(response, 4, body);
        return response;
    }

private:
    Bytes saved_;
};
}  // namespace cube_dongle::studio

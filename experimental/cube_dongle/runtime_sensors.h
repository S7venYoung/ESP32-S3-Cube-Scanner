#pragma once
#include <deque>
#include "combo_engine.h"
namespace cube_dongle::studio {
struct SensorBinding {
    Binding binding{Kind::Media, 1, 0};
    uint16_t tap_ms = 5;
};
struct SensorSettings {
    std::array<std::array<std::array<SensorBinding, 2>, 4>, 4> bindings{};
    SensorSettings() {
        for (auto& sensor : bindings)
            for (auto& layer : sensor)
                layer[1].binding.value = 2;
    }
    static bool Valid(const SensorBinding& value) {
        Binding checked;
        return value.tap_ms >= 1 && value.tap_ms <= 5000 &&
               BindingFromWire(Behavior(value.binding) * 2, Param1(value.binding),
                               Param2(value.binding), checked) &&
               checked.kind == value.binding.kind && checked.value == value.binding.value &&
               checked.modifiers == value.binding.modifiers && checked.kind != Kind::ModTap &&
               checked.kind != Kind::LayerTap;
    }
    Bytes Export() const {
        Bytes out{'C', 'S', 'N', 1};
        for (const auto& sensor : bindings)
            for (const auto& layer : sensor)
                for (const auto& entry : layer) {
                    out.push_back(uint8_t(entry.binding.kind));
                    out.push_back(entry.binding.value);
                    out.push_back(entry.binding.value >> 8);
                    out.push_back(entry.binding.modifiers);
                    out.push_back(entry.tap_ms);
                    out.push_back(entry.tap_ms >> 8);
                }
        const auto hash = Model::Hash(out.data(), out.size());
        for (unsigned i = 0; i < 4; ++i)
            out.push_back(hash >> (8 * i));
        return out;
    }
    bool Load(const Bytes& bytes) {
        if (bytes.size() != 200 || std::memcmp(bytes.data(), "CSN\1", 4))
            return false;
        uint32_t hash = 0;
        for (unsigned i = 0; i < 4; ++i)
            hash |= uint32_t(bytes[196 + i]) << (8 * i);
        if (hash != Model::Hash(bytes.data(), 196))
            return false;
        SensorSettings next;
        size_t at = 4;
        for (auto& sensor : next.bindings)
            for (auto& layer : sensor)
                for (auto& entry : layer) {
                    if (bytes[at] > uint8_t(Kind::RuntimeMacro))
                        return false;
                    entry.binding = {Kind(bytes[at]),
                                     uint16_t(bytes[at + 1] | (uint16_t(bytes[at + 2]) << 8)),
                                     bytes[at + 3]};
                    entry.tap_ms = bytes[at + 4] | (uint16_t(bytes[at + 5]) << 8);
                    at += 6;
                    if (!Valid(entry))
                        return false;
                }
        *this = next;
        return true;
    }
    static Bytes Encode(const SensorBinding& entry) {
        Bytes out;
        Number(out, 1, Behavior(entry.binding));
        Number(out, 2, Param1(entry.binding));
        Number(out, 3, Param2(entry.binding));
        Number(out, 4, entry.tap_ms);
        return out;
    }
    Bytes Handle(const Bytes& payload, bool unlocked, uint8_t hardware, const Model& model,
                 const Rpc::Save& save) {
        Reader request(payload);
        const auto* op = request.Operation();
        if (!request.valid || !op || op->wire != 2)
            return CustomError("Invalid sensor request");
        Reader args(op->data, op->size);
        if (!args.valid || !args.Numbers({1, 2}))
            return CustomError("Invalid sensor arguments");
        Bytes body, response;
        if (op->tag == 4) {
            for (unsigned i = 0; i < 4; ++i)
                if (hardware & (1u << i)) {
                    Bytes info;
                    Number(info, 1, i);
                    Text(info, 2, "Split Encoder " + std::to_string(i + 1));
                    Message(body, 1, info);
                }
            Message(response, 5, body);
            return response;
        }
        const auto index = args.Value(1), layer = args.Value(2);
        if (index >= 4 || !(hardware & (1u << index)))
            return CustomError("Encoder hardware not installed");
        if (op->tag == 3) {
            for (size_t n = 0; n < model.count; ++n) {
                const auto id = model.order[n];
                Bytes item;
                Number(item, 1, id);
                Message(item, 2, Encode(bindings[index][id][0]));
                Message(item, 3, Encode(bindings[index][id][1]));
                Message(body, 1, item);
            }
            Message(response, 4, body);
            return response;
        }
        if (!unlocked)
            return CustomError("Studio is locked");
        const auto* field = args.Get(3);
        if ((op->tag != 1 && op->tag != 2) || layer >= 4 || !model.Active(layer) || !field ||
            field->wire != 2)
            return CustomError("Invalid sensor binding");
        Reader binding(field->data, field->size);
        SensorBinding entry;
        if (!binding.valid || !binding.Numbers({1, 2, 3, 4}) || binding.Value(1) > UINT32_MAX / 2 ||
            !BindingFromWire(binding.Value(1) * 2, binding.Value(2), binding.Value(3),
                             entry.binding) ||
            binding.Value(4) > 5000)
            return CustomError("Invalid sensor behavior");
        entry.tap_ms = binding.Value(4) ? binding.Value(4) : 5;
        if (!Valid(entry))
            return CustomError("Unsupported sensor behavior");
        auto next = *this;
        next.bindings[index][layer][op->tag - 1] = entry;
        const bool okay = save(next.Export());
        if (okay)
            *this = next;
        Number(body, 1, okay);
        Message(response, op->tag + 1, body);
        return response;
    }
};
class SensorEngine {
public:
    void Register(unsigned index, uint16_t triggers) {
        if (index < 4) {
            triggers_[index] = triggers;
            remainders_[index] = {};
            Cancel();
        }
    }
    bool Input(unsigned index, int32_t whole, int32_t fractional, size_t layer,
               const SensorSettings& config) {
        if (index >= 4 || !triggers_[index] || triggers_[index] > 360 || layer >= 4)
            return false;
        int64_t ticks = fractional;
        if (whole) {
            auto& remainder = remainders_[index][layer];
            remainder += int64_t(whole) * 1000000 + fractional;
            const int64_t unit = int64_t(360 / triggers_[index]) * 1000000;
            ticks = remainder / unit;
            remainder %= unit;
        }
        if (ticks < -32 || ticks > 32 || queue_.size() + std::abs(ticks) > 32) {
            Cancel();
            return false;
        }
        for (int64_t i = 0; i < std::abs(ticks); ++i)
            queue_.push_back(config.bindings[index][layer][ticks < 0 ? 1 : 0]);
        return true;
    }
    struct Result {
        ReportFrame report;
        uint16_t actions = 0;
        uint8_t macro_mask = 0, layers = 0;
    };
    Result Tick(uint32_t now) {
        Result result;
        if (active_ && int32_t(now - deadline_) >= 0) {
            active_ = false;
            return result;
        }
        bool started = false;
        if (!active_ && !queue_.empty()) {
            current_ = queue_.front();
            queue_.pop_front();
            active_ = true;
            deadline_ = now + current_.tap_ms;
            started = true;
        }
        if (!active_)
            return result;
        const auto& binding = current_.binding;
        if (binding.kind == Kind::Key) {
            result.report.keyboard[0] = binding.modifiers;
            if (binding.value >= 0xe0 && binding.value <= 0xe7)
                result.report.keyboard[0] |= 1u << (binding.value - 0xe0);
            else
                result.report.keyboard[2] = binding.value;
        } else if (binding.kind == Kind::Media)
            result.report.media = binding.value;
        else if (binding.kind == Kind::MomentaryLayer)
            result.layers = 1u << binding.value;
        else if (started && binding.kind == Kind::HostAction)
            result.actions = 1u << binding.value;
        else if (started && binding.kind == Kind::RuntimeMacro)
            result.macro_mask = 1u << binding.value;
        return result;
    }
    bool NeedsTick() const { return active_ || !queue_.empty(); }
    void Cancel() {
        queue_.clear();
        active_ = false;
    }

private:
    std::array<uint16_t, 4> triggers_{};
    std::array<std::array<int64_t, 4>, 4> remainders_{};
    std::deque<SensorBinding> queue_;
    SensorBinding current_;
    uint32_t deadline_ = 0;
    bool active_ = false;
};
}  // namespace cube_dongle::studio

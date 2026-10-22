#pragma once
#include "runtime_tapdance.h"

namespace cube_dongle::studio {
// NimBLE-thread only. Named configuration is snapshotted at the first press.
class TapDanceEngine {
public:
    void Cancel() {
        sequences_ = {};
        held_ = {};
        held_active_ = {};
        pending_hold_ = {};
        pulses_ = {};
        captured_ = {};
        previous_ = {};
        actions_ = 0;
        macros_ = 0;
        inhibited_ = true;
    }
    void Prepare(const SplitState::Snapshot& raw, uint32_t now) {
        raw_ = raw;
        now_ = now;
        actions_ = 0;
        macros_ = 0;
        if (inhibited_) {
            bool any = false;
            for (auto b : raw)
                any |= b != 0;
            if (any)
                return;
            inhibited_ = false;
        }
        for (size_t p = 0; p < 61; ++p) {
            bool interrupted = false;
            for (size_t q = 0; q < 61; ++q)
                if (q != p && Down(raw, q) && !Down(previous_, q))
                    interrupted = true;
            if (!Down(raw, p)) {
                if (pending_hold_[p])
                    Pulse(Tap(held_[p]));
                held_[p] = {};
                held_active_[p] = false;
                pending_hold_[p] = false;
                captured_[p] = false;
            } else if (pending_hold_[p] &&
                       (interrupted || uint32_t(now - hold_at_[p]) >= hold_term_[p])) {
                const auto binding = held_[p];
                held_[p] = binding.kind == Kind::LayerTap
                               ? Binding{Kind::MomentaryLayer, uint16_t(binding.value >> 8), 0}
                               : Binding{Kind::Key, uint16_t(binding.value >> 8), 0};
                pending_hold_[p] = false;
            }
        }
        for (auto& s : sequences_) {
            if (!s.used)
                continue;
            bool interrupt = false;
            for (size_t p = 0; p < 61; p++)
                if (p != s.position && Down(raw, p) && !Down(previous_, p))
                    interrupt = true;
            if (uint32_t(now - s.at) >= s.interval || interrupt)
                Resolve(s, Down(previous_, s.position) && Down(raw, s.position));
        }
    }
    template <class Keyboard>
    void Finish(const Keyboard& preview, const DanceModel& model, SplitState::Snapshot& filtered,
                SplitState::Snapshot& taps) {
        if (inhibited_) {
            previous_ = raw_;
            return;
        }
        for (size_t p = 0; p < 61; p++) {
            if (Down(raw_, p) && !Down(previous_, p)) {
                const auto binding = preview.PressedBinding(p);
                auto* pending = Find(p);
                if (binding.kind != Kind::RuntimeTapDance) {
                    if (pending)
                        Resolve(*pending, false);
                } else {
                    captured_[p] = true;
                    Press(p, binding.value, model, true);
                }
            }
            if (Down(taps, p)) {
                const auto binding = preview.BindingAt(p);
                if (binding.kind == Kind::RuntimeTapDance) {
                    Press(p, binding.value, model, false);
                    Clear(taps, p);
                }
            }
            if (captured_[p])
                Clear(filtered, p);
        }
        previous_ = raw_;
    }
    uint16_t TakeActions() {
        const auto a = actions_;
        actions_ = 0;
        return a;
    }
    uint8_t TakeMacros() {
        const auto a = macros_;
        macros_ = 0;
        return a;
    }
    void TappingTerm(unsigned value) {
        if (value && value <= 5000)
            term_ = value;
    }
    std::vector<Binding> TakePulses() {
        std::vector<Binding> out;
        for (auto& p : pulses_)
            if (p.used) {
                out.push_back(p.binding);
                p = {};
            }
        return out;
    }
    uint8_t Layers() const {
        uint8_t layers = 0;
        for (const auto& b : held_)
            if (b.kind == Kind::MomentaryLayer)
                layers |= 1u << b.value;
        return layers;
    }
    static ReportFrame BindingReport(const Binding& b) {
        ReportFrame f;
        if (b.kind == Kind::Key) {
            f.keyboard[0] = b.modifiers;
            if (b.value >= 0xe0)
                f.keyboard[0] |= 1u << (b.value - 0xe0);
            else
                f.keyboard[2] = b.value;
        } else if (b.kind == Kind::Media)
            f.media = b.value;
        return f;
    }
    ReportFrame Report() const {
        ReportFrame result;
        for (const auto& b : held_)
            result = MergeReports(result, BindingReport(b));
        return result;
    }
    bool NeedsTick() const {
        for (const auto& s : sequences_)
            if (s.used)
                return true;
        for (auto pending : pending_hold_)
            if (pending)
                return true;
        for (const auto& p : pulses_)
            if (p.used)
                return true;
        return false;
    }

private:
    struct Sequence {
        bool used = false;
        uint8_t position = 0, count = 0, maximum = 0;
        uint32_t at = 0, interval = 200;
        std::array<DanceAction, 3> actions{};
    };
    struct Pulse {
        bool used = false;
        Binding binding;
        uint32_t at = 0;
    };
    std::array<Sequence, 8> sequences_{};
    std::array<Binding, 61> held_{};
    std::array<bool, 61> held_active_{};
    std::array<bool, 61> pending_hold_{};
    std::array<uint32_t, 61> hold_at_{};
    std::array<unsigned, 61> hold_term_{};
    unsigned term_ = 200;
    std::array<Pulse, 16> pulses_{};
    std::array<bool, 61> captured_{};
    SplitState::Snapshot previous_{}, raw_{};
    uint32_t now_ = 0;
    uint16_t actions_ = 0;
    uint8_t macros_ = 0;
    bool inhibited_ = false;
    static bool Down(const SplitState::Snapshot& s, size_t p) { return s[p / 8] & (1u << (p % 8)); }
    static void Clear(SplitState::Snapshot& s, size_t p) { s[p / 8] &= uint8_t(~(1u << (p % 8))); }
    Sequence* Find(size_t p) {
        for (auto& s : sequences_)
            if (s.used && s.position == p)
                return &s;
        return nullptr;
    }
    static Binding Tap(const Binding& b) {
        return {Kind::Key, uint16_t(b.value & 255), b.modifiers};
    }
    void Pulse(const Binding& binding) {
        if (binding.kind != Kind::Key && binding.kind != Kind::Media)
            return;
        for (auto& p : pulses_)
            if (!p.used) {
                p = {true, binding, now_};
                return;
            }
        Cancel();  // Overflow releases synthetic state; never leave modifiers held.
    }
    void Resolve(Sequence& s, bool held) {
        auto binding = s.actions[s.count - 1].Decode();
        if (held)
            held_active_[s.position] = true;
        if (binding.kind == Kind::HostAction)
            actions_ |= 1u << binding.value;
        else if (binding.kind == Kind::RuntimeMacro)
            macros_ |= 1u << binding.value;
        else if (held) {
            held_[s.position] = binding;
            if (binding.kind == Kind::ModTap || binding.kind == Kind::LayerTap) {
                pending_hold_[s.position] = true;
                hold_at_[s.position] = now_;
                hold_term_[s.position] = term_;
            }
        } else {
            if (binding.kind == Kind::ModTap || binding.kind == Kind::LayerTap)
                binding = Tap(binding);
            Pulse(binding);
        }
        s = {};
    }
    void Press(size_t position, uint16_t id, const DanceModel& model, bool held) {
        if (inhibited_)
            return;
        auto* s = Find(position);
        if (s) {
            ++s->count;
            s->at = now_;
        } else {
            unsigned active = 0;
            for (const auto& state : sequences_)
                active += state.used;
            for (auto selected : held_active_)
                active += selected;
            if (active >= 8)
                return;
            const auto entry = model.entries.find(id);
            if (entry == model.entries.end() || !entry->second.Count())
                return;
            for (auto& free : sequences_)
                if (!free.used) {
                    s = &free;
                    break;
                }
            if (!s)
                return;  // Bounded concurrency: never evict or replay another key.
            *s = {true,           uint8_t(position),    1, uint8_t(entry->second.Count()), now_,
                  model.interval, entry->second.actions};
        }
        if (s->count == s->maximum)
            Resolve(*s, held);
    }
};
}  // namespace cube_dongle::studio

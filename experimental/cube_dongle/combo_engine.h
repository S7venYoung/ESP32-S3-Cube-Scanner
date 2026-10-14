#pragma once
#include "runtime_combos.h"

namespace cube_dongle::studio {
inline ReportFrame MergeReports(ReportFrame a, const ReportFrame& b) {
    std::array<bool, 256> keys{};
    a.keyboard[0] |= b.keyboard[0];
    a.media |= b.media;
    for (size_t i = 2; i < 8; ++i) {
        keys[a.keyboard[i]] = true;
        keys[b.keyboard[i]] = true;
        a.keyboard[i] = 0;
    }
    size_t count = 0;
    for (size_t usage = 4; usage < 0xe0; ++usage)
        if (keys[usage]) {
            if (count < 6)
                a.keyboard[2 + count] = usage;
            ++count;
        }
    if (count > 6 || keys[1])
        for (size_t i = 2; i < 8; ++i)
            a.keyboard[i] = 1;
    return a;
}
struct ComboResult {
    SplitState::Snapshot filtered{}, taps{};
    ReportFrame report;
    uint16_t actions = 0;
    uint8_t macro_mask = 0, layers = 0;
};
class ComboEngine {
public:
    bool Configure(const ComboModel& model) {
        if (!Neutral() || !model.timeout || model.timeout > 1000 || model.prior_idle > 60000)
            return false;
        for (const auto& combo : model.entries)
            if (!ComboModel::Valid(combo))
                return false;
        model_ = model;
        return true;
    }
    void Cancel() {
        keys_ = {};
        active_.fill(false);
        inhibited_ = true;
    }
    bool Neutral() const {
        for (const auto& key : keys_)
            if (key.down)
                return false;
        return true;
    }
    bool Pending() const {
        for (const auto& key : keys_)
            if (key.pending)
                return true;
        return false;
    }
    bool NeedsTick() const { return Pending() || inhibited_; }
    ComboResult Apply(const SplitState::Snapshot& raw, uint32_t now, size_t layer) {
        ComboResult result;
        if (inhibited_) {
            bool down = false;
            for (auto byte : raw)
                down |= byte != 0;
            if (down)
                return result;
            inhibited_ = false;
            last_press_ = now;
        }
        std::array<bool, 61> rising{};
        bool pressed = false;
        const uint32_t prior = now - last_press_;
        for (size_t p = 0; p < keys_.size(); ++p) {
            auto& key = keys_[p];
            const bool down = raw[p / 8] & (1u << (p % 8));
            if (down && !key.down) {
                key = Key{};
                key.down = true;
                key.at = now;
                key.prior = prior;
                rising[p] = pressed = true;
            } else if (!down && key.down) {
                if (key.pending && !key.consumed)
                    Set(result.taps, p);
                key = {};
            }
        }
        if (pressed)
            last_press_ = now;
        for (size_t p = 0; p < keys_.size(); ++p)
            if (rising[p]) {
                bool candidate = false;
                for (const auto& combo : model_.entries)
                    if (Eligible(combo, layer) && Contains(combo, p) && CanStart(combo, now))
                        candidate = true;
                keys_[p].pending = candidate;
                keys_[p].emitted = !candidate;
            }
        for (size_t i = 0; i < active_.size(); ++i) {
            if (!active_[i])
                continue;
            const auto& combo = model_.entries[i];
            bool any = false, all = true;
            for (auto p : combo.keys) {
                any |= keys_[p].down;
                all &= keys_[p].down;
            }
            const bool slow =
                combo.slow_override == 1 || (combo.slow_override == 0 && model_.slow_release);
            active_[i] = slow ? any : all;
            if (active_[i])
                Consume(combo);
        }
        for (size_t tries = 0; tries < active_.size(); ++tries) {
            size_t best = active_.size();
            for (size_t i = 0; i < active_.size(); ++i) {
                const auto& combo = model_.entries[i];
                if (active_[i] || !Eligible(combo, layer) || !Ready(combo, now))
                    continue;
                if (best == active_.size() || combo.keys.size() > model_.entries[best].keys.size())
                    best = i;
            }
            if (best == active_.size())
                break;
            const auto& combo = model_.entries[best];
            bool wait = false;
            for (const auto& larger : model_.entries) {
                if (!Eligible(larger, layer) || larger.keys.size() <= combo.keys.size() ||
                    !CanStart(larger, now))
                    continue;
                bool superset = true;
                for (auto p : combo.keys)
                    superset &= Contains(larger, p);
                uint32_t age = 0;
                for (auto p : larger.keys)
                    if (keys_[p].down)
                        age = std::max(age, now - keys_[p].at);
                if (superset && age < Timeout(larger))
                    wait = true;
            }
            if (wait)
                break;
            active_[best] = true;
            Consume(combo);
            if (combo.binding.kind == Kind::HostAction)
                result.actions |= 1u << combo.binding.value;
            if (combo.binding.kind == Kind::RuntimeMacro)
                result.macro_mask |= 1u << combo.binding.value;
        }
        for (size_t p = 0; p < keys_.size(); ++p) {
            auto& key = keys_[p];
            if (!key.down || key.consumed)
                continue;
            if (key.pending) {
                uint32_t timeout = 0;
                for (const auto& combo : model_.entries)
                    if (Eligible(combo, layer) && Contains(combo, p) && CanStart(combo, now))
                        timeout = std::max(timeout, Timeout(combo));
                if (now - key.at >= timeout) {
                    key.pending = false;
                    key.emitted = true;
                }
            }
            if (key.emitted)
                Set(result.filtered, p);
        }
        for (size_t i = 0; i < active_.size(); ++i)
            if (active_[i]) {
                const auto binding = model_.entries[i].binding;
                ReportFrame report;
                if (binding.kind == Kind::Key) {
                    report.keyboard[0] = binding.modifiers;
                    if (binding.value >= 0xe0 && binding.value <= 0xe7)
                        report.keyboard[0] |= 1u << (binding.value - 0xe0);
                    else
                        report.keyboard[2] = binding.value;
                } else if (binding.kind == Kind::Media)
                    report.media = binding.value;
                else if (binding.kind == Kind::MomentaryLayer)
                    result.layers |= 1u << binding.value;
                result.report = MergeReports(result.report, report);
            }
        return result;
    }

private:
    struct Key {
        bool down = false, pending = false, emitted = false, consumed = false;
        uint32_t at = 0, prior = 0;
    };
    static void Set(SplitState::Snapshot& snapshot, size_t p) { snapshot[p / 8] |= 1u << (p % 8); }
    static bool Contains(const Combo& combo, size_t p) {
        return std::find(combo.keys.begin(), combo.keys.end(), p) != combo.keys.end();
    }
    static bool Eligible(const Combo& combo, size_t layer) {
        return combo.enabled && combo.keys.size() >= 2 &&
               (!combo.layers || (layer < 4 && (combo.layers & (1u << layer))));
    }
    uint32_t Timeout(const Combo& combo) const {
        return combo.timeout ? combo.timeout : model_.timeout;
    }
    bool CanStart(const Combo& combo, uint32_t now) const {
        size_t first = keys_.size();
        uint32_t age = 0;
        for (auto p : combo.keys) {
            const auto& key = keys_[p];
            if (key.consumed || key.emitted)
                return false;
            if (key.down && (first == keys_.size() || now - key.at > age)) {
                first = p;
                age = now - key.at;
            }
        }
        const uint32_t idle = combo.prior_idle ? combo.prior_idle : model_.prior_idle;
        return first != keys_.size() && keys_[first].prior >= idle;
    }
    bool Ready(const Combo& combo, uint32_t now) const {
        if (!CanStart(combo, now))
            return false;
        uint32_t oldest = 0, newest = UINT32_MAX;
        for (auto p : combo.keys) {
            if (!keys_[p].down || !keys_[p].pending)
                return false;
            oldest = std::max(oldest, now - keys_[p].at);
            newest = std::min(newest, now - keys_[p].at);
        }
        return oldest - newest <= Timeout(combo);
    }
    void Consume(const Combo& combo) {
        for (auto p : combo.keys)
            if (keys_[p].down) {
                keys_[p].consumed = true;
                keys_[p].pending = keys_[p].emitted = false;
            }
    }
    ComboModel model_;
    std::array<Key, 61> keys_{};
    std::array<bool, 8> active_{};
    uint32_t last_press_ = 0;
    bool inhibited_ = false;
};
}  // namespace cube_dongle::studio

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cube_dongle {
// Native ZMK split position snapshots, not Prospector advertising telemetry.
// Call only from the single dongle worker, after peer identity and encryption checks.
class SplitState {
public:
    static constexpr size_t kBytes = 16;
    using Snapshot = std::array<uint8_t, kBytes>;
    using Ownership = std::array<Snapshot, 2>;

    explicit SplitState(const Ownership& owners) : owners_(owners) {}

    bool Apply(size_t side, const uint8_t* data, size_t size, bool trusted_encrypted_peer) {
        if (side >= 2 || !data || size != kBytes || !trusted_encrypted_peer)
            return false;
        // Reject the complete frame if it contains another half's positions.
        for (size_t i = 0; i < size; ++i) {
            if (data[i] & static_cast<uint8_t>(~owners_[side][i]))
                return false;
        }
        for (size_t i = 0; i < size; ++i)
            states_[side][i] = data[i];
        return true;
    }

    bool Disconnect(size_t side) {
        if (side >= 2)
            return false;
        states_[side].fill(0);
        return true;
    }

    Snapshot Combined() const {
        Snapshot result{};
        for (size_t i = 0; i < kBytes; ++i)
            result[i] = states_[0][i] | states_[1][i];
        return result;
    }

private:
    Ownership owners_;
    std::array<Snapshot, 2> states_{};
};

enum class Kind { None, Transparent, Key, MomentaryLayer, Media, HostAction, RuntimeMacro };
struct Binding {
    Kind kind = Kind::None;
    uint16_t value = 0;  // Keyboard usage, layer, media bits, or explicit host command
    uint8_t modifiers = 0;
};

// Standard boot keyboard body: modifiers, reserved, six key usages.
using KeyboardReport = std::array<uint8_t, 8>;
enum class Endpoint { Usb, Bluetooth };

template <size_t Positions, size_t Layers>
class KeyboardState {
public:
    static_assert(Positions <= 128 && Layers > 0 && Layers <= 16);
    using Keymap = std::array<std::array<Binding, Positions>, Layers>;
    explicit KeyboardState(const Keymap& keymap) : keymap_(keymap) {
        for (size_t i = 0; i < Layers; ++i)
            order_[i] = i;
    }
    // Replace future bindings on the host thread; held keys keep their old binding.
    void SetKeymap(const Keymap& keymap) { keymap_ = keymap; }
    void SetExternalLayers(uint16_t mask) { external_layers_ = mask; }
    bool SetLayerOrder(const std::array<size_t, Layers>& order, size_t count) {
        if (!count || count > Layers)
            return false;
        std::array<bool, Layers> seen{};
        for (size_t i = 0; i < count; ++i) {
            if (order[i] >= Layers || seen[order[i]])
                return false;
            seen[order[i]] = true;
        }
        order_ = order;
        layer_count_ = count;
        return true;
    }

    // Process releases first. Latch press-time bindings so changing layers never
    // changes which usage is released. Simultaneous layer presses resolve first.
    void Apply(const SplitState::Snapshot& snapshot) {
        actions_ = 0;
        for (size_t p = 0; p < Positions; ++p) {
            if (pressed_[p] && !Down(snapshot, p)) {
                pressed_[p] = false;
                latched_[p] = {};
            }
        }
        for (size_t p = 0; p < Positions; ++p) {
            if (!pressed_[p] && Down(snapshot, p)) {
                auto binding = Resolve(p);
                if (binding.kind == Kind::MomentaryLayer)
                    Latch(p, binding);
            }
        }
        for (size_t p = 0; p < Positions; ++p) {
            if (!pressed_[p] && Down(snapshot, p))
                Latch(p, Resolve(p));
        }
    }

    KeyboardReport Report() const {
        KeyboardReport report{};
        std::array<bool, 256> usages{};
        for (size_t p = 0; p < Positions; ++p) {
            const auto& binding = latched_[p];
            if (!pressed_[p] || binding.kind != Kind::Key)
                continue;
            report[0] |= binding.modifiers;
            if (binding.value >= 0xe0 && binding.value <= 0xe7) {
                report[0] |= static_cast<uint8_t>(1u << (binding.value - 0xe0));
            } else if (binding.value >= 4 && binding.value < 0xe0) {
                usages[binding.value] = true;
            }
        }
        size_t count = 0;
        for (size_t usage = 4; usage < 0xe0; ++usage) {
            if (!usages[usage])
                continue;
            if (count < 6)
                report[2 + count] = static_cast<uint8_t>(usage);
            ++count;
        }
        // HID ErrorRollOver, never silently discard a seventh held key.
        if (count > 6)
            for (size_t i = 2; i < 8; ++i)
                report[i] = 1;
        return report;
    }
    uint8_t MediaReport() const {
        uint8_t bits = 0;
        for (size_t p = 0; p < Positions; ++p)
            if (pressed_[p] && latched_[p].kind == Kind::Media)
                bits |= latched_[p].value & 7;
        return bits;
    }
    uint16_t Actions() const { return actions_; }
    Binding PressedBinding(size_t position) const {
        return position < Positions && pressed_[position] ? latched_[position] : Binding{};
    }
    size_t ActiveLayer() const {
        size_t layer = order_[0];
        for (size_t i = 1; i < layer_count_; ++i) {
            if (external_layers_ & (1u << order_[i]))
                layer = order_[i];
            for (size_t p = 0; p < Positions; ++p)
                if (pressed_[p] && latched_[p].kind == Kind::MomentaryLayer &&
                    latched_[p].value == order_[i])
                    layer = order_[i];
        }
        return layer;
    }

private:
    static bool Down(const SplitState::Snapshot& snapshot, size_t position) {
        return snapshot[position / 8] & (1u << (position % 8));
    }
    Binding Resolve(size_t position) const {
        std::array<bool, Layers> active{};
        active[order_[0]] = true;
        for (size_t layer = 0; layer < Layers; ++layer)
            active[layer] = active[layer] || (external_layers_ & (1u << layer));
        for (size_t p = 0; p < Positions; ++p) {
            const auto& b = latched_[p];
            if (pressed_[p] && b.kind == Kind::MomentaryLayer && b.value < Layers)
                active[b.value] = true;
        }
        for (size_t index = layer_count_; index-- > 0;) {
            const auto layer = order_[index];
            if (active[layer] && keymap_[layer][position].kind != Kind::Transparent)
                return keymap_[layer][position];
        }
        return {};
    }
    void Latch(size_t position, Binding binding) {
        pressed_[position] = true;
        latched_[position] = binding;
        if (binding.kind == Kind::HostAction && binding.value < 16)
            actions_ |= 1u << binding.value;
    }
    Keymap keymap_;
    std::array<size_t, Layers> order_{};
    size_t layer_count_ = Layers;
    uint16_t external_layers_ = 0;
    std::array<bool, Positions> pressed_{};
    std::array<Binding, Positions> latched_{};
    uint16_t actions_ = 0;
};

// Adapter must return true only when the transport accepted this report.
// Old output must accept release-all before another host can receive input.
class OutputRouter {
public:
    template <typename Send>
    bool Switch(Endpoint next, Send send) {
        if (next == endpoint_)
            return true;
        if (!send(endpoint_, KeyboardReport{}))
            return false;
        endpoint_ = next;
        return true;
    }
    template <typename Send>
    bool Publish(const KeyboardReport& report, Send send) {
        return send(endpoint_, report);
    }
    Endpoint Selected() const { return endpoint_; }

private:
    Endpoint endpoint_ = Endpoint::Usb;
};
}  // namespace cube_dongle

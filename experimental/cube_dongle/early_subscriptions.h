#pragma once
#include <array>
#include <cstdint>

namespace cube_dongle {
// Host-queue-only staging for CCCD WRITE/RESTORE delivered before delayed CONNECT.
// Never invent subscriptions, persist them, or carry them across disconnect/reset.
class EarlySubscriptions {
  public:
    enum : uint8_t { Keyboard = 1, Media = 2, Boot = 4, Pointer = 8, Studio = 16 };
    bool Observe(uint16_t handle, uint8_t channel, bool enabled) {
        if (!channel || (channel & (channel - 1)) || channel > Studio || handle == 0xffff)
            return false;
        Slot* slot = nullptr;
        for (auto& candidate : slots_)
            if (candidate.used && candidate.handle == handle)
                slot = &candidate;
        if (!slot)
            for (auto& candidate : slots_)
                if (!candidate.used) {
                    slot = &candidate;
                    *slot = {handle, 0, true};
                    break;
                }
        if (!slot)
            return false;
        slot->flags = enabled ? uint8_t(slot->flags | channel)
                              : uint8_t(slot->flags & ~channel);
        return true;
    }
    uint8_t Take(uint16_t handle) {
        for (auto& slot : slots_)
            if (slot.used && slot.handle == handle) {
                const auto flags = slot.flags;
                slot = {};
                return flags;
            }
        return 0;
    }
    void Clear() { slots_ = {}; }

  private:
    struct Slot {
        uint16_t handle = 0;
        uint8_t flags = 0;
        bool used = false;
    };
    std::array<Slot, 3> slots_{};
};
}  // namespace cube_dongle

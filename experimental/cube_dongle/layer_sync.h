#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace cube_dongle {
// Latest state, not a history of transitions: two bounded slots independent of
// the RPC/dashboard queue. Caller owns the relay queue lock.
class LayerSync {
  public:
    struct Update {
        size_t side = 0;
        uint8_t layer = 0;
        uintptr_t generation = 0;
    };
    void Observe(size_t side, bool ready, uintptr_t generation, uint8_t layer, uint64_t now) {
        auto& slot = slots_[side];
        if (!ready || layer == 255) {
            slot = {};
            return;
        }
        if (!slot.seen || slot.update.generation != generation || slot.update.layer != layer ||
            now >= slot.next) {
            slot.update = {side, layer, generation};
            slot.pending = slot.seen = true;
            slot.next = now + 1000;
        }
    }
    bool Take(Update& update) {
        for (size_t offset = 0; offset < slots_.size(); ++offset) {
            const auto side = (next_side_ + offset) % slots_.size();
            auto& slot = slots_[side];
            if (!slot.pending)
                continue;
            update = slot.update;
            slot.pending = false;
            next_side_ = (side + 1) % slots_.size();
            return true;
        }
        return false;
    }

  private:
    struct Slot {
        Update update;
        uint64_t next = 0;
        bool seen = false, pending = false;
    };
    std::array<Slot, 2> slots_{};
    size_t next_side_ = 0;
};
}  // namespace cube_dongle

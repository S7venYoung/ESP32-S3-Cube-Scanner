#pragma once
#include <cstdint>

namespace cube_dongle {
// A failed advertising attempt must not invalidate a connection whose SDK
// CONNECT callback is still waiting for remote-version/feature exchange.
class HostEpoch {
  public:
    uintptr_t Begin() {
        if (pending_)
            return 0;
        if (++sequence_ == 0)
            ++sequence_;
        pending_ = sequence_;
        return pending_;
    }
    void Complete(int result) {
        if (!result && pending_)
            active_ = pending_;
        pending_ = 0;
    }
    bool Accept(uintptr_t epoch) const {
        return epoch && (epoch == active_ || epoch == pending_);
    }
    bool Pending() const { return pending_ != 0; }
    void Invalidate() {
        active_ = pending_ = 0;
    }

  private:
    uintptr_t sequence_ = 1, active_ = 0, pending_ = 0;
};
}  // namespace cube_dongle

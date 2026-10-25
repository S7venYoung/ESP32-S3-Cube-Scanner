#pragma once
#include <cstdint>

namespace cube_dongle {
// Cookies belong to a host lifetime, not an advertising attempt. The SDK can
// allocate a peripheral connection and delay CONNECT while another advertisement
// starts successfully. Both attempts must retain the same valid callback cookie.
class HostEpoch {
  public:
    uintptr_t Begin() {
        if (pending_)
            return 0;
        if (active_)
            pending_ = active_;
        else {
            if (++sequence_ == 0)
                ++sequence_;
            pending_ = sequence_;
        }
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

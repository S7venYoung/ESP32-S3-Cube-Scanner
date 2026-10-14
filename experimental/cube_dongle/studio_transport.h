#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <vector>

namespace cube_dongle::studio {
// Serialize external access with the adapter's mutex; host callbacks never wait
// for a UI response or perform flash writes. Indications advance only on ACK.
class BlePipe {
public:
    void Reset(uint32_t epoch) {
        epoch_ = epoch;
        rx_start_ = rx_size_ = 0;
        tx_.clear();
        tx_bytes_ = offset_ = pending_ = 0;
    }
    uint32_t Epoch() const { return epoch_; }
    bool Receive(const uint8_t* bytes, size_t size) {
        if (!bytes || size > rx_.size() - rx_size_)
            return false;
        for (size_t i = 0; i < size; ++i)
            rx_[(rx_start_ + rx_size_++) % rx_.size()] = bytes[i];
        return true;
    }
    size_t Read(uint8_t* bytes, size_t capacity) {
        const size_t count = std::min(capacity, rx_size_);
        for (size_t i = 0; i < count; ++i)
            bytes[i] = rx_[(rx_start_ + i) % rx_.size()];
        rx_start_ = (rx_start_ + count) % rx_.size();
        rx_size_ -= count;
        return count;
    }
    bool Write(const uint8_t* bytes, size_t size, uint32_t epoch) {
        if (epoch != epoch_ || !bytes || !size || size > 8194 || tx_.size() >= 4 ||
            tx_bytes_ + size > 16388)
            return false;
        tx_.emplace_back(bytes, bytes + size);
        tx_bytes_ += size;
        return true;
    }
    size_t Next(uint8_t* bytes, size_t mtu) {
        if (pending_ || tx_.empty() || mtu <= 3)
            return 0;
        pending_ = std::min({mtu - 3, tx_.front().size() - offset_, size_t(512)});
        std::copy_n(tx_.front().data() + offset_, pending_, bytes);
        return pending_;
    }
    bool Pending() const { return pending_ != 0; }
    void Retry() { pending_ = 0; }
    void Confirm() {
        if (!pending_ || tx_.empty())
            return;
        offset_ += pending_;
        pending_ = 0;
        if (offset_ == tx_.front().size()) {
            tx_bytes_ -= tx_.front().size();
            tx_.pop_front();
            offset_ = 0;
        }
    }

private:
    uint32_t epoch_ = 0;
    std::array<uint8_t, 4096> rx_{};
    size_t rx_start_ = 0, rx_size_ = 0;
    std::deque<std::vector<uint8_t>> tx_;
    size_t tx_bytes_ = 0, offset_ = 0, pending_ = 0;
};
}  // namespace cube_dongle::studio

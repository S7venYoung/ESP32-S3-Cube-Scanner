#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cube_dongle {
// Native ZMK split relay framing (pinned cormoran/zmk e5c9b69).
// Header: sequence/end, chunk length, event-name length. Payload: name + data.
class SplitRelay {
public:
    using Bytes = std::vector<uint8_t>;
    void Reset() {
        data_.clear();
        name_size_ = 0;
    }
    bool Feed(const uint8_t* bytes, size_t size, std::string& name, Bytes& body) {
        if (!bytes || size < 4 || bytes[1] != size - 3 || !bytes[2] || bytes[2] > 32) {
            Reset();
            return false;
        }
        const uint8_t seq = bytes[0] & 127;
        if (!data_.empty() && (seq != next_ || bytes[2] != name_size_)) {
            Reset();
            return false;
        }
        if (data_.empty())
            name_size_ = bytes[2];
        if (data_.size() + size - 3 > name_size_ + 255u) {
            Reset();
            return false;
        }
        data_.insert(data_.end(), bytes + 3, bytes + size);
        next_ = (seq + 1) & 127;
        if (!(bytes[0] & 128))
            return false;
        if (data_.size() < name_size_ ||
            std::find(data_.begin(), data_.begin() + name_size_, 0) != data_.begin() + name_size_) {
            Reset();
            return false;
        }
        name.assign(data_.begin(), data_.begin() + name_size_);
        body.assign(data_.begin() + name_size_, data_.end());
        Reset();
        return true;
    }
    static std::vector<Bytes> Encode(const std::string& name, const Bytes& data, uint16_t mtu,
                                     uint8_t& sequence) {
        if (name.empty() || name.size() > 32 || name.find('\0') != std::string::npos ||
            data.size() > 255 || mtu < 7)
            return {};
        Bytes payload(name.begin(), name.end());
        payload.insert(payload.end(), data.begin(), data.end());
        std::vector<Bytes> chunks;
        const size_t maximum = std::min<size_t>(255, mtu - 6);
        for (size_t at = 0; at < payload.size();) {
            const size_t count = std::min(maximum, payload.size() - at);
            const bool last = at + count == payload.size();
            Bytes frame{uint8_t((sequence++ & 127) | (last ? 128 : 0)), uint8_t(count),
                        uint8_t(name.size())};
            frame.insert(frame.end(), payload.begin() + at, payload.begin() + at + count);
            chunks.push_back(std::move(frame));
            at += count;
        }
        sequence &= 127;
        return chunks;
    }

private:
    Bytes data_;
    uint8_t next_ = 0, name_size_ = 0;
};
}  // namespace cube_dongle

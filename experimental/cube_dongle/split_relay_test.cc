#include "split_relay.h"
#include <cassert>
#include <random>
int main() {
    using cube_dongle::SplitRelay;
    uint8_t sequence = 125;
    SplitRelay::Bytes data(195, 42), decoded;
    std::string name;
    auto frames = SplitRelay::Encode("KDr", data, 23, sequence);
    SplitRelay parser;
    for (size_t i = 0; i < frames.size(); ++i) {
        auto& frame = frames[i];
        assert(frame.size() <= 20);
        assert(parser.Feed(frame.data(), frame.size(), name, decoded) == (i + 1 == frames.size()));
    }
    assert(name == "KDr" && decoded == data);
    assert(!parser.Feed(frames[0].data(), frames[0].size(), name, decoded));
    assert(!parser.Feed(frames[2].data(), frames[2].size(), name, decoded));
    assert(SplitRelay::Encode("KDr", SplitRelay::Bytes(256), 23, sequence).empty());
    assert(SplitRelay::Encode("KDr", data, 6, sequence).empty());
    std::mt19937 random(42);
    for (unsigned i = 0; i < 10000; ++i) {
        SplitRelay::Bytes fuzz(random() % 300);
        for (auto& byte : fuzz)
            byte = random();
        parser.Feed(fuzz.data(), fuzz.size(), name, decoded);
    }
}

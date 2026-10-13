#include <cassert>
#include <vector>
#include "peer_binding.h"
#include "split_state.h"
#include "velvet_positions.h"

using namespace cube_dongle;

int main() {
    PeerBinding identity;
    assert(ParseIdentity("C1:23:45:67:89:AB", 1, identity));
    assert(identity.bytes[0] == 0xab && identity.bytes[5] == 0xc1);
    const auto unchanged = identity.bytes;
    assert(!ParseIdentity("81:23:45:67:89:AB", 1, identity));
    assert(!ParseIdentity("00:00:00:00:00:00", 0, identity));
    assert(!ParseIdentity("FF:FF:FF:FF:FF:FF", 1, identity));
    assert(!ParseIdentity("C1:23:45:67:89:GG", 1, identity));
    assert(!ParseIdentity("C1:23:45:67:89:ABx", 1, identity));
    assert(!ParseIdentity("C1:23:45:67:89:AB", 2, identity));
    assert(identity.bytes == unchanged);
    BindRequest bind;
    assert(ParseBindCommand("DONGLE BIND L C1:23:45:67:89:AB 1", bind));
    assert(bind.side == 0 && bind.stored == "C1:23:45:67:89:AB 1");
    assert(ParseBindCommand("DONGLE BIND R 01:23:45:67:89:AB 0", bind));
    assert(bind.side == 1 && bind.identity.type == 0);
    assert(!ParseBindCommand("DONGLE BIND R 01:23:45:67:89:AB 1", bind));
    assert(!ParseBindCommand("DONGLE BIND X C1:23:45:67:89:AB 1", bind));
    assert(!ParseBindCommand("DONGLE BIND L C1:23:45:67:89:AB 1 extra", bind));
    assert(bind.side == 1);
    constexpr auto velvet = VelvetOwnership();
    unsigned left_count = 0, right_count = 0;
    for (size_t p = 0; p < 128; ++p) {
        const bool left = velvet[0][p / 8] & (1u << (p % 8));
        const bool right = velvet[1][p / 8] & (1u << (p % 8));
        assert(!(left && right));
        assert((left || right) == (p < 61));
        left_count += left;
        right_count += right;
    }
    assert(left_count == 32 && right_count == 29);
    SplitState::Ownership owners{};
    owners[0][0] = 0x0f;
    owners[1][0] = 0xf0;
    SplitState split(owners);
    SplitState::Snapshot left{}, right{};
    left[0] = 1;
    right[0] = 0x10;
    assert(!split.Apply(0, left.data(), 15, true));
    assert(!split.Apply(0, nullptr, 16, true));
    assert(!split.Apply(2, left.data(), 16, true));
    assert(!split.Apply(0, left.data(), 16, false));
    assert(split.Apply(0, left.data(), 16, true));
    assert(split.Apply(1, right.data(), 16, true));
    assert(split.Combined()[0] == 0x11);
    assert(!split.Apply(0, right.data(), 16, true));
    assert(split.Combined()[0] == 0x11);
    split.Disconnect(0);
    assert(split.Combined()[0] == 0x10);
    split.Disconnect(1);
    assert(split.Combined()[0] == 0);

    KeyboardState<16, 3>::Keymap map{};
    for (auto& layer : map)
        for (auto& b : layer)
            b = {Kind::Transparent, 0, 0};
    for (size_t p = 0; p < 10; ++p)
        map[0][p] = {Kind::Key, static_cast<uint8_t>(4 + p), 0};
    map[0][10] = {Kind::MomentaryLayer, 1, 0};
    map[0][11] = {Kind::Key, 0xe3, 0};
    map[0][12] = {Kind::Key, 4, 0};  // duplicate usage on another position
    map[0][13] = {Kind::MomentaryLayer, 1, 0};
    map[1][0] = {Kind::Key, 40, 2};
    KeyboardState<16, 3> keyboard(map);
    SplitState::Snapshot positions{};
    positions[1] = 0x0c;  // layer + Command
    positions[0] = 1;
    keyboard.Apply(positions);
    assert(keyboard.Report()[0] == (8 | 2));
    assert(keyboard.Report()[2] == 40);
    positions[1] = 0;  // releasing layer must not remap held Enter into A
    keyboard.Apply(positions);
    assert(keyboard.Report()[2] == 40);
    assert(keyboard.Report()[0] == 2);
    keyboard.Apply({});
    assert(keyboard.Report() == KeyboardReport{});
    positions = {};
    positions[0] = 1;
    positions[1] = 0x10;
    keyboard.Apply(positions);
    assert(keyboard.Report()[2] == 4 && keyboard.Report()[3] == 0);
    positions[0] = 0;
    keyboard.Apply(positions);
    assert(keyboard.Report()[2] == 4);
    positions[0] = 0x7f;
    keyboard.Apply(positions);
    for (size_t i = 2; i < 8; ++i)
        assert(keyboard.Report()[i] == 1);
    positions[0] = 0x3f;
    positions[1] = 0;
    keyboard.Apply(positions);
    assert(keyboard.Report()[2] == 4 && keyboard.Report()[7] == 9);
    keyboard.Apply({});
    positions = {};
    positions[1] = 0x24;  // two keys hold same layer
    keyboard.Apply(positions);
    positions[1] = 0x20;  // release only one layer key
    positions[0] = 1;
    keyboard.Apply(positions);
    assert(keyboard.Report()[2] == 40);

    OutputRouter output;
    std::vector<KeyboardReport> sent;
    assert(
        !output.Switch(Endpoint::Bluetooth, [](Endpoint, const KeyboardReport&) { return false; }));
    assert(output.Selected() == Endpoint::Usb);
    assert(output.Switch(Endpoint::Bluetooth, [&](Endpoint endpoint, const KeyboardReport& report) {
        assert(endpoint == Endpoint::Usb);
        sent.push_back(report);
        return true;
    }));
    assert(sent.size() == 1 && sent[0] == KeyboardReport{});
    assert(output.Publish(keyboard.Report(), [](Endpoint endpoint, const KeyboardReport&) {
        return endpoint == Endpoint::Bluetooth;
    }));
}

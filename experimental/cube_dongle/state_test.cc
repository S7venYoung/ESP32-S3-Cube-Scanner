#include <cassert>
#include <vector>
#include "split_state.h"

using namespace cube_dongle;

int main() {
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

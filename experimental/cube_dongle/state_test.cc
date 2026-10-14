#include <cassert>
#include <vector>
#include "../../main/scanner/bluetooth_voice.h"
#include "auto_binding.h"
#include "host_control.h"
#include "peer_binding.h"
#include "report_queue.h"
#include "split_state.h"
#include "typing_activity.h"
#include "velvet_keymap.h"
#include "velvet_positions.h"

using namespace cube_dongle;

int main() {
    {
        std::vector<uint8_t> advert{19, 0x21};
        advert.insert(advert.end(), kPairSet.begin(), kPairSet.end());
        advert.push_back(1);
        advert.push_back(0);
        assert(AutoSide(advert.data(), advert.size()) == 0);
        advert.back() = 1;
        assert(AutoSide(advert.data(), advert.size()) == 1);
        for (size_t n = 0; n < advert.size(); ++n)
            assert(AutoSide(advert.data(), n) == -1);
        advert.back() = 2;
        assert(AutoSide(advert.data(), advert.size()) == -1);
        advert.back() = 0;
        auto duplicate = advert;
        duplicate.insert(duplicate.end(), advert.begin(), advert.end());
        assert(AutoSide(duplicate.data(), duplicate.size()) == -1);
        advert[2] ^= 1;
        assert(AutoSide(advert.data(), advert.size()) == -1);
        std::array<uint8_t, 18> identity{};
        identity[0] = 1;
        identity[1] = 1;
        std::copy(kPairSet.begin(), kPairSet.end(), identity.begin() + 2);
        assert(AutoIdentity(identity.data(), 18, 1));
        assert(!AutoIdentity(identity.data(), 18, 0));
        assert(!AutoIdentity(identity.data(), 17, 1));
        uint8_t address[6] = {1, 2, 3, 4, 5, 0xc0};
        AutoCandidate candidate;
        candidate.Observe(address, 1);
        candidate.Observe(address, 1);
        assert(candidate.seen && !candidate.ambiguous);
        ++address[0];
        candidate.Observe(address, 1);
        assert(candidate.ambiguous);
    }
    assert(CubeBluetoothVoiceCommand("你好小智，切换下一个蓝牙。") == "DONGLE BT NEXT");
    assert(CubeBluetoothVoiceCommand("小智请切换到蓝牙二号") == "DONGLE BT 2");
    assert(CubeBluetoothVoiceCommand("连接蓝牙5") == "DONGLE BT 5");
    assert(CubeBluetoothVoiceCommand("切换蓝牙配置三") == "DONGLE BT 3");
    for (const auto* invalid : {"不要切换蓝牙二", "蓝牙二", "切换到蓝牙6", "切换到蓝牙二然后清空",
                                "清空蓝牙配对", "讨论一下切换蓝牙二"})
        assert(CubeBluetoothVoiceCommand(invalid).empty());
    HostRequest host_request;
    assert(ParseHostCommand("DONGLE BT NEXT", host_request) &&
           host_request.command == HostCommand::Next);
    for (int slot = 1; slot <= 5; ++slot) {
        assert(ParseHostCommand("DONGLE BT " + std::to_string(slot), host_request));
        assert(host_request.command == HostCommand::Select && host_request.profile == slot - 1);
    }
    assert(ParseHostCommand("DONGLE OUTPUT AUTO", host_request) &&
           host_request.command == HostCommand::Auto);
    assert(ParseHostCommand("DONGLE OUTPUT USB", host_request) &&
           host_request.command == HostCommand::Usb);
    assert(ParseHostCommand("DONGLE OUTPUT BLE", host_request) &&
           host_request.command == HostCommand::Ble);
    assert(ParseHostCommand("DONGLE BT CLEAR", host_request) &&
           host_request.command == HostCommand::Clear);
    assert(ParseHostCommand("DONGLE BT CLEAR ALL", host_request) &&
           host_request.command == HostCommand::ClearAll);
    assert(ParseHostCommand("DONGLE BT PAIR", host_request) &&
           host_request.command == HostCommand::Pair);
    assert(ParseHostCommand("DONGLE DISABLE", host_request) &&
           host_request.command == HostCommand::Disable);
    for (const auto& invalid :
         {"DONGLE BT 0", "DONGLE BT 6", "DONGLE BT 01", "DONGLE BT 1x", "DONGLE BT 1 ",
          "DONGLE OUTPUT", "DONGLE OUTPUT bluetooth", "DONGLE BT CLEAR ALL NOW"})
        assert(!ParseHostCommand(invalid, host_request));
    HostBarrier host_barrier;
    ReportFrame held;
    held.keyboard[0] = 8;
    held.keyboard[2] = 4;
    assert(!host_barrier.Accept(held, true));
    assert(!host_barrier.Accept({}, false));
    assert(host_barrier.Accept({}, true));
    assert(host_barrier.Accept(held, true));
    host_barrier.Begin();
    ReportFrame media_held;
    media_held.media = 1;
    assert(!host_barrier.Accept(media_held, true));
    assert(!host_barrier.Accept({}, false));
    assert(host_barrier.Waiting() && host_barrier.Accept({}, true));
    KeyboardState<61, 3> activity_keyboard(VelvetKeymap());
    TypingActivity activity;
    SplitState::Snapshot typing{};
    typing[13 / 8] |= 1u << (13 % 8);
    activity_keyboard.Apply(typing);
    activity.Observe(typing, activity_keyboard, 0);
    assert(activity.Wpm(0) == 0);  // reconnect with a held Q is not a new press
    typing = {};
    activity_keyboard.Apply(typing);
    activity.Observe(typing, activity_keyboard, 100);
    typing[13 / 8] |= 1u << (13 % 8);
    activity_keyboard.Apply(typing);
    activity.Observe(typing, activity_keyboard, 200);
    assert(activity.Wpm(200) == 12);
    activity.Observe(typing, activity_keyboard, 300);
    assert(activity.Wpm(300) == 12);   // duplicate snapshot adds no typing
    typing[25 / 8] |= 1u << (25 % 8);  // mac Command, not a word
    activity_keyboard.Apply(typing);
    activity.Observe(typing, activity_keyboard, 400);
    assert(activity.Wpm(400) == 12);
    assert(UiModifiers(activity_keyboard.Report()[0]) == 4);
    assert(activity.Wpm(950) == 0);  // idle even if Q remains held
    activity.Reset();
    activity.Observe(typing, activity_keyboard, 10000);
    assert(activity.Wpm(10000) == 0);
    assert(UiModifiers(0xff) == 15 && UiModifiers(0) == 0);
    assert(UiModifiers(0x10) == 1 && UiModifiers(0x40) == 2 && UiModifiers(0x20) == 8);
    TypingActivity other_hand;
    other_hand.Observe({}, activity_keyboard, 10000);
    assert(other_hand.Wpm(10000) == 0);  // independent left/right histories
    KeyboardState<61, 3> real_keyboard(VelvetKeymap());
    SplitState::Snapshot real_positions{};
    auto down = [&](size_t p) { real_positions[p / 8] |= 1u << (p % 8); };
    down(13);  // Q
    real_keyboard.Apply(real_positions);
    assert(real_keyboard.Report()[2] == 20);
    real_positions = {};
    down(57);  // momentary NVA layer
    down(15);  // Volume Down
    down(16);  // Volume Up, must not drop either media button
    real_keyboard.Apply(real_positions);
    assert(real_keyboard.Report() == KeyboardReport{});
    assert(real_keyboard.MediaReport() == 3);
    assert(real_keyboard.ActiveLayer() == 1);
    real_positions = {};
    down(55);  // Space
    down(54);  // momentary SYS
    down(47);  // BT_SEL 2 in SYS
    real_keyboard.Apply(real_positions);
    assert(real_keyboard.Actions() == 4);
    assert(real_keyboard.ActiveLayer() == 2);
    assert(real_keyboard.Report()[2] == 44);
    real_keyboard.Apply(real_positions);
    assert(real_keyboard.Actions() == 0);  // no repeated destructive command on a snapshot
    real_keyboard.Apply({});
    assert(real_keyboard.Report() == KeyboardReport{} && real_keyboard.MediaReport() == 0);
    assert(real_keyboard.ActiveLayer() == 0);

    ReportQueue<2> queue;
    ReportFrame a, b;
    a.keyboard[2] = 4;
    b.keyboard[2] = 5;
    assert(queue.Push(a) && queue.Push({}));
    assert(queue.Front()->keyboard[2] == 4);
    queue.Pop();
    assert(queue.Front()->Neutral());
    queue.Pop();
    assert(!queue.Front());
    assert(queue.Push(a) && queue.Push(b));
    assert(!queue.Push(a));  // full queue must release, never silently lose a key-up
    assert(queue.Front()->Neutral() && queue.Inhibited());
    assert(!queue.Push(b));
    assert(queue.Push({}) && !queue.Inhibited());
    queue.Pop();
    assert(queue.Push(a));
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

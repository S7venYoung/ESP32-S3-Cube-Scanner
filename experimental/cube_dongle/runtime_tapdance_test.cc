#include <cassert>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "tapdance_engine.h"
using namespace cube_dongle;
using namespace cube_dongle::studio;
Bytes Req(unsigned op, Bytes args = {}) {
    Bytes out;
    Message(out, op, args);
    return out;
}
bool Error(const Bytes& b) { return Reader(b).Get(1) != nullptr; }
int main(int argc, char**) {
    if (argc > 1) {
        RuntimeTapDances rpc;
        Bytes disk;
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "REBOOT") {
                rpc = RuntimeTapDances{};
                if (!disk.empty())
                    assert(rpc.Load(disk));
                std::cout << "OK" << std::endl;
                continue;
            }
            Bytes data;
            for (size_t i = 2; i + 1 < line.size(); i += 2)
                data.push_back(static_cast<uint8_t>(std::stoul(line.substr(i, 2), nullptr, 16)));
            auto out = rpc.Handle(data, line.substr(0, 2) == "01", [&](const Bytes& b) {
                disk = b;
                return true;
            });
            for (auto b : out)
                std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
            std::cout << std::endl;
        }
        return 0;
    }
    RuntimeTapDances dances;
    Bytes disk;
    unsigned writes = 0;
    auto save = [&](const Bytes& b) {
        disk = b;
        ++writes;
        return true;
    };
    auto create = [&](const std::string& name, bool persist = false) {
        Bytes a;
        Text(a, 1, name);
        Number(a, 2, persist);
        return dances.Handle(Req(6, a), true, save);
    };
    auto set = [&](unsigned id, unsigned count, DanceAction action, bool persist = false) {
        Bytes a;
        Number(a, 1, id);
        Number(a, 2, count);
        Message(a, 3, action.Encode());
        Number(a, 4, persist);
        return dances.Handle(Req(2, a), true, save);
    };
    auto key = [](unsigned usage) { return DanceAction{kKp, 0x70000 | usage, 0}; };
    assert(!Error(create("first")) && writes == 1 && dances.model.next_id == 1);
    RuntimeTapDances reboot;
    assert(reboot.Load(disk) && reboot.model.entries.empty() && reboot.model.next_id == 1);
    assert(Error(set(0, 2, key(5))));
    assert(!Error(set(0, 1, key(4))) && !Error(set(0, 2, key(5))) && !Error(set(0, 3, key(6))));
    assert(Error(set(0, 1, {})));  // Can't orphan double/triple.
    assert(Error(set(0, 2, {kRuntimeTapDance, 0, 0})));
    const auto old = dances.model.Export();
    Bytes rename;
    Number(rename, 1, 0);
    Text(rename, 2, "renamed");
    assert(!Error(dances.Handle(Req(7, rename), true, save)) &&
           dances.model.entries.at(0).name == "renamed");
    assert(!Error(dances.Handle(Req(10), true, save)) && reboot.Load(disk));
    assert(reboot.model.entries.at(0).actions[2].p1 == 0x70006);
    assert(!Error(create("temporary")) && dances.model.next_id == 2);
    assert(!Error(dances.Handle(Req(11), true, save)) && dances.model.entries.size() == 1 &&
           dances.model.next_id == 2);
    assert(!Error(create("third")) && dances.model.entries.count(2) &&
           !dances.model.entries.count(1));
    Bytes del;
    Number(del, 1, 0);
    assert(!Error(dances.Handle(Req(8, del), true, save)) && !dances.model.entries.count(0));
    assert(reboot.Load(disk) && !reboot.model.entries.count(0) && reboot.model.next_id == 3);
    assert(Error(create(std::string(25, 'x'))) && Error(create(std::string("\xc0\x80", 2))));
    Bytes badname;
    Text(badname, 1, "locked");
    assert(Error(dances.Handle(Req(6, badname), false, save)));
    const auto before = dances.model.Export();
    assert(Error(dances.Handle(Req(6, badname), true, [](const Bytes&) { return false; })) &&
           dances.model.Export() == before);
    auto global = dances.Handle(Req(9), false, save);
    Reader gr(global);
    const auto* gf = gr.Get(4);
    assert(gf && Reader(gf->data, gf->size).Value(8) == 2);
    assert(Reader(gf->data, gf->size).Value(3) == 1);
    Bytes record = disk;
    record.back() ^= 1;
    assert(!reboot.Load(record));
    Binding td;
    assert(BindingFromWire(kRuntimeTapDance * 2, 65535, 0, td) && td.kind == Kind::RuntimeTapDance);
    assert(!BindingFromWire(kRuntimeTapDance * 2, 65536, 0, td));
    Model keymap;
    keymap.map[0][0] = {Kind::RuntimeTapDance, 2, 0};
    Model loaded;
    assert(loaded.Load(keymap.Export()) && loaded.map[0][0].kind == Kind::RuntimeTapDance);
    assert(!Rpc::BehaviorDetails(kRuntimeTapDance).empty());

    DanceModel model;
    model.next_id = 1;
    model.entries[0] = {"test", {key(4), key(5), key(6)}};
    KeyboardState<61, 4>::Keymap map{};
    map[0][0] = {Kind::RuntimeTapDance, 0, 0};
    map[0][1] = {Kind::Key, 7, 0};
    map[0][2] = {Kind::RuntimeTapDance, 0, 0};
    KeyboardState<61, 4> keyboard(map);
    TapDanceEngine engine;
    SplitState::Snapshot raw{};
    std::vector<Binding> pulses;
    auto step = [&](uint32_t now) {
        engine.Prepare(raw, now);
        auto preview = keyboard;
        preview.SetExternalLayers(engine.Layers());
        preview.Apply(raw, now);
        auto filtered = raw;
        SplitState::Snapshot taps{};
        engine.Finish(preview, model, filtered, taps);
        pulses = engine.TakePulses();
        keyboard.SetExternalLayers(engine.Layers());
        keyboard.Apply(filtered, now);
        return MergeReports({keyboard.Report(), keyboard.MediaReport()}, engine.Report());
    };
    auto down = [&](size_t p) { raw[p / 8] |= 1u << (p % 8); };
    auto up = [&](size_t p) { raw[p / 8] &= uint8_t(~(1u << (p % 8))); };
    down(0);
    assert(step(0).keyboard[2] == 0);
    up(0);
    assert(step(10).keyboard[2] == 0);
    assert(step(199).keyboard[2] == 0);
    step(200);
    assert(pulses.size() == 1 && pulses[0].value == 4);
    assert(step(210).keyboard[2] == 0);
    down(0);
    step(300);
    up(0);
    step(310);
    down(0);
    assert(step(400).keyboard[2] == 0);
    up(0);
    step(410);
    step(600);
    assert(pulses.size() == 1 && pulses[0].value == 5);
    assert(step(610).keyboard[2] == 0);
    down(0);
    step(700);
    up(0);
    step(710);
    down(0);
    step(750);
    up(0);
    step(760);
    down(0);
    assert(step(800).keyboard[2] == 6);
    assert(step(900).keyboard[2] == 6);
    up(0);
    assert(step(901).keyboard[2] == 0);
    down(0);
    step(1000);
    down(1);
    auto interrupted = step(1010);
    assert(interrupted.keyboard[2] == 4 && interrupted.keyboard[3] == 7);
    up(0);
    assert(step(1020).keyboard[2] == 7);
    up(1);
    step(1030);
    down(0);
    step(1100);
    up(0);
    step(1110);
    model.entries[0].actions[1] = key(29);
    down(0);
    step(1200);
    up(0);
    step(1210);
    step(1400);
    assert(pulses.size() == 1 && pulses[0].value == 5);
    step(1410);
    down(0);
    step(1500);
    up(0);
    step(1510);
    down(0);
    step(1700);  // Exact deadline = old single + new sequence.
    assert(pulses.size() == 1 && pulses[0].value == 4);
    up(0);
    step(1710);
    step(1900);
    assert(pulses.size() == 1 && pulses[0].value == 4);
    step(1910);
    down(0);
    step(2000);
    engine.Cancel();
    assert(engine.Report().keyboard[2] == 0);
    step(2500);
    assert(!engine.NeedsTick());
    up(0);
    step(2510);
    model.entries[0].actions = {DanceAction{kMo, 1, 0}, {}, {}};
    map[1][1] = {Kind::Key, 8, 0};
    keyboard.SetKeymap(map);
    down(0);
    step(2600);
    assert(engine.Layers() == 2);
    down(1);
    assert(step(2610).keyboard[2] == 8);
    up(0);
    step(2620);
    up(1);
    step(2630);
    assert(engine.Layers() == 0 && engine.Report().keyboard[2] == 0);
    return 0;
}

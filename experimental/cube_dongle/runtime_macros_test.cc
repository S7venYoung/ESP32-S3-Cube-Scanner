#include "runtime_macros.h"
#include <cassert>
using namespace cube_dongle::studio;
Bytes Operation(unsigned tag, Bytes body = {}) {
    Bytes out;
    Message(out, tag, body);
    return out;
}
bool Failed(const Bytes& bytes) { return Reader(bytes).Get(1) != nullptr; }
int main() {
    RuntimeMacros macros;
    Bytes saved;
    auto save = [&saved](const Bytes& value) {
        saved = value;
        return true;
    };
    Bytes create;
    Text(create, 1, "hello");
    assert(Failed(macros.Handle(Operation(12, create), false, save)));
    assert(!Failed(macros.Handle(Operation(12, create), true, save)));
    assert(macros.model.entries[0].name == "hello");
    assert(Failed(macros.Handle(Operation(12, create), true, save)));
    Bytes binding;
    Number(binding, 1, kKp);
    Number(binding, 2, 0x70004);
    const auto tap = Operation(3, binding);
    assert(MacroModel::Step(tap));
    Bytes append;
    Number(append, 1, 0);
    Message(append, 2, tap);
    assert(!Failed(macros.Handle(Operation(11, append), true, save)));
    assert(Failed(macros.Handle(Operation(9), true, [](const Bytes&) { return false; })));
    assert(saved.empty());
    assert(!Failed(macros.Handle(Operation(9), true, save)));
    MacroModel restored;
    assert(restored.Load(saved));
    auto corrupt = saved;
    corrupt.back() ^= 1;
    assert(!restored.Load(corrupt));
    MacroPlayer player;
    assert(player.Start(restored.entries[0], restored.tap_ms, 100));
    assert(!player.Start(restored.entries[0], restored.tap_ms, 100));
    player.Tick(100);
    auto report = player.Merge({});
    assert(report.keyboard[2] == 4);
    cube_dongle::ReportFrame physical;
    physical.keyboard[2] = 5;
    report = player.Merge(physical);
    assert(report.keyboard[2] == 4 && report.keyboard[3] == 5);
    player.Tick(119);
    assert(!player.Merge({}).Neutral());
    player.Tick(120);
    assert(player.Merge({}).Neutral());
    player.Tick(122);
    assert(!player.Active());
    Bytes keys;
    Data(keys, 1, reinterpret_cast<const uint8_t*>("\x84\x05"), 2);
    Macro sequence{"sequence", {Operation(5, keys)}};
    assert(MacroModel::Step(sequence.steps[0]));
    assert(player.Start(sequence, 20, 200));
    player.Tick(200);
    report = player.Merge({});
    assert(report.keyboard[0] == 2 && report.keyboard[2] == 4);
    player.Tick(220);
    assert(player.Merge({}).Neutral());
    player.Tick(222);
    report = player.Merge({});
    assert(report.keyboard[0] == 0 && report.keyboard[2] == 5);
    player.Cancel();
    assert(player.Merge(physical) == physical);
    Bytes remove;
    Text(remove, 1, "hello");
    assert(!Failed(macros.Handle(Operation(13, remove), true, save)));
    assert(restored.Load(saved) && restored.entries[0].name.empty());
    cube_dongle::Binding macro_binding;
    assert(BindingFromWire(12, 7, 0, macro_binding));
    assert(macro_binding.kind == cube_dongle::Kind::RuntimeMacro);
    assert(!BindingFromWire(12, 8, 0, macro_binding));
}

#include <cassert>
#include "studio_protocol.h"
using namespace cube_dongle;
int main() {
    KeyboardState<61, 4>::Keymap map{};
    map[0][0] = {Kind::ModTap, 0xe004, 0};
    map[0][1] = {Kind::LayerTap, 0x0105, 0};
    map[0][2] = {Kind::Key, 6, 0};
    map[1][2] = {Kind::Key, 7, 0};
    KeyboardState<61, 4> keys(map);
    SplitState::Snapshot state{};
    state[0] = 1;
    keys.Apply(state, 100);
    assert(keys.Report() == KeyboardReport{} && keys.NeedsTick());
    state[0] = 0;
    keys.Apply(state, 199);
    assert(keys.Report()[2] == 4);
    assert(keys.FlushTaps() && keys.Report() == KeyboardReport{});
    state[0] = 1;
    keys.Apply(state, 1000);
    keys.Apply(state, 1200);
    assert(keys.Report()[0] == 1 && !keys.NeedsTick());
    state[0] = 0;
    keys.Apply(state, 1300);
    assert(keys.Report() == KeyboardReport{} && !keys.FlushTaps());
    state[0] = 2;
    keys.Apply(state, 2000);
    state[0] = 6;
    keys.Apply(state, 2001);
    assert(keys.ActiveLayer() == 1 && keys.Report()[2] == 7);
    state[0] = 0;
    keys.Apply(state, 2002);
    state[0] = 1;
    keys.Apply(state, UINT32_MAX - 100);
    keys.Apply(state, 100);
    assert(keys.Report()[0] == 1);
    state[0] = 0;
    keys.Apply(state, 101);
    state[0] = 1;
    keys.Apply(state, 200);
    keys.CancelHoldTaps();
    keys.Apply(state, 500);
    assert(keys.Report() == KeyboardReport{});
    state[0] = 0;
    keys.Apply(state, 501);
    assert(!keys.FlushTaps());
    for (auto b : {map[0][0], map[0][1]}) {
        Binding decoded;
        assert(studio::BindingFromWire(studio::Behavior(b) * 2, studio::Param1(b),
                                       studio::Param2(b), decoded));
        assert(decoded.kind == b.kind && decoded.value == b.value &&
               decoded.modifiers == b.modifiers);
    }
    studio::Model model;
    model.map = map;
    studio::Model restored;
    assert(restored.Load(model.Export()));
    assert(restored.map[0][0].kind == Kind::ModTap);
}

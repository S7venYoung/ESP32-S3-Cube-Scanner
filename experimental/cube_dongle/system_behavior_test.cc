#include "fast_keymap.h"
#include <cassert>

int main() {
    using namespace cube_dongle;
    using namespace cube_dongle::studio;
    for (uint32_t id = kBootloader; id <= kUsbOutput; ++id) {
        Binding binding;
        assert(BindingFromWire(id * 2, 0, 0, binding));
        assert(binding.kind == Kind::HostAction);
        assert(Behavior(binding) == id && Param1(binding) == 0 && Param2(binding) == 0);
        assert(!BindingFromWire(id * 2, 1, 0, binding));
        assert(!BindingFromWire(id * 2, 0, 1, binding));
        assert(BindingFromWire(id * 2, 0, 0, binding));
        const auto details = Rpc::BehaviorDetails(id);
        Reader metadata(details.data(), details.size());
        assert(metadata.Get(2) && metadata.Get(2)->size);
        Model model, restored;
        model.map[0][0] = binding;
        assert(restored.Load(model.Export()));
        assert(Behavior(restored.map[0][0]) == id);
        KeyboardState<1, 1>::Keymap map{};
        map[0][0] = binding;
        KeyboardState<1, 1> keyboard(map);
        SplitState::Snapshot state{};
        state[0] = 1;
        keyboard.Apply(state);
        assert(keyboard.Actions() == (1u << binding.value));
        keyboard.Apply(state);
        assert(keyboard.Actions() == 0);  // Held keys do not reboot repeatedly.
        state[0] = 0;
        keyboard.Apply(state);
        assert(keyboard.Actions() == 0);
    }
    assert(Rpc::BehaviorDetails(kLastBehavior + 1).empty());
    Rpc rpc;
    Bytes request;
    Number(request, 1, 1);
    const auto bytes = FastKeymap(request, rpc);
    Reader response(bytes.data(), bytes.size());
    const auto* summary = response.Get(2);
    assert(summary);
    Reader metadata(summary->data, summary->size);
    assert(metadata.Value(6) == kLastBehavior);
}

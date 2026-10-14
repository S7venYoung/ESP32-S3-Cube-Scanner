#include "runtime_sensors.h"
#include <cassert>
using namespace cube_dongle;
using namespace cube_dongle::studio;
int main() {
    SensorSettings settings, restored;
    assert(restored.Load(settings.Export()));
    SensorEngine engine;
    assert(!engine.Input(0, 15, 0, 0, settings));
    engine.Register(0, 24);
    assert(engine.Input(0, 15, 0, 0, settings));
    assert(engine.Tick(0).report.media == 1);
    assert(engine.Tick(5).report.Neutral());
    assert(!engine.NeedsTick());
    assert(engine.Input(0, 0, -2, 0, settings));
    assert(engine.Tick(10).report.media == 2);
    assert(engine.Tick(15).report.Neutral());
    assert(engine.Tick(20).report.media == 2);
    engine.Cancel();
    assert(engine.Tick(25).report.Neutral());
    assert(!engine.Input(0, 0, 33, 0, settings));
    Model model;
    Bytes args, binding, request, stored;
    Number(args, 1, 0);
    Number(args, 2, 1);
    Number(binding, 1, kKp);
    Number(binding, 2, 0x70004);
    Number(binding, 4, 20);
    Message(args, 3, binding);
    Message(request, 1, args);
    assert(
        Reader(settings.Handle(request, true, 0, model, [](const Bytes&) { return true; })).Get(1));
    assert(Reader(settings.Handle(request, false, 1, model, [](const Bytes&) {
               return true;
           })).Get(1));
    auto reply = settings.Handle(request, true, 1, model, [&](const Bytes& bytes) {
        stored = bytes;
        return true;
    });
    assert(Reader(reply).Get(2) && restored.Load(stored));
    assert(restored.bindings[0][1][0].binding.value == 4);
    engine.Register(0, 24);
    assert(engine.Input(0, 15, 0, 1, restored));
    assert(engine.Tick(UINT32_MAX - 10).report.keyboard[2] == 4);
    assert(engine.Tick(10).report.Neutral());
}

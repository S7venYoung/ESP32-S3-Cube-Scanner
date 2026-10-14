#include "runtime_pointer.h"
#include <cassert>
using namespace cube_dongle;
using namespace cube_dongle::studio;
int main() {
    PointerEngine engine;
    PointerReport report;
    auto packet = [](uint8_t type, uint16_t code, int32_t value, bool sync) {
        return std::array<uint8_t, 8>{type,
                                      uint8_t(code),
                                      uint8_t(code >> 8),
                                      uint8_t(value),
                                      uint8_t(uint32_t(value) >> 8),
                                      uint8_t(uint32_t(value) >> 16),
                                      uint8_t(uint32_t(value) >> 24),
                                      uint8_t(sync)};
    };
    auto move = packet(2, 0, 10, true);
    assert(!engine.Input(0, move.data(), move.size(), 0, 0, report));
    engine.Register(0, true);
    assert(engine.Input(0, move.data(), move.size(), 0, 0, report) && report.x == 10);
    auto click = packet(1, 0x110, 1, true);
    assert(engine.Input(0, click.data(), click.size(), 1, 0, report) && report.buttons == 1);
    assert(engine.Input(0, move.data(), move.size(), 2, 0, report) && report.buttons == 1);
    std::array<ProcessorConfig, 4> config;
    config[0].values[3] = 2;
    config[0].values[16] = 1;
    engine.Configure(config);
    assert(engine.Input(0, move.data(), move.size(), 3, 0, report) && report.x == -20 &&
           report.buttons == 1);
    config[0].values[28] = 1;
    config[0].values[21] = 50;
    config[0].values[6] = 1;
    config[0].values[7] = 2;
    engine.Configure(config);
    assert(engine.Input(0, move.data(), move.size(), 10, 0, report));
    assert(engine.Inertia() == 1);
    auto inertial = engine.Tick(26, 0);
    assert(inertial.size() == 1 && inertial[0].x < 0);
    assert(engine.Layers(60) == 4);
    engine.CancelMotion();
    assert(engine.Tick(100, 0).empty() && engine.Layers(100) == 0);
    engine.Register(0, false);
    assert(engine.Buttons() == 0);
    PointerSettings settings, restored;
    assert(restored.Load(settings.Export()));
    Model model;
    Bytes args, request, stored;
    Number(args, 1, 0);
    Number(args, 2, 3);
    Message(request, 3, args);
    auto notify = [](const Bytes&) {};
    assert(Reader(settings.Handle(
                      request, true, 0, model, [](const Bytes&) { return true; }, notify))
               .Get(1));
    assert(Reader(settings.Handle(
                      request, false, 1, model, [](const Bytes&) { return true; }, notify))
               .Get(1));
    assert(Reader(settings.Handle(
                      request, true, 1, model,
                      [&](const Bytes& b) {
                          stored = b;
                          return true;
                      },
                      notify))
               .Get(4));
    assert(settings.live[0].values[3] == 3 && restored.Load(stored) &&
           restored.live[0].values[3] == 3);
    args.clear();
    request.clear();
    Number(args, 1, 0);
    Number(args, 2, 999);
    Message(request, 4, args);
    settings.Handle(request, true, 1, model, [](const Bytes&) { return false; }, notify);
    assert(settings.live[0].values[4] == 1);
}

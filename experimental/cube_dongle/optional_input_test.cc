#include "optional_input.h"
#include <cassert>

int main() {
    const auto wire = cube_dongle::PointerBytes({-32767, 32767, -2, 3, 5});
    assert((wire == std::array<uint8_t, 7>{5, 1, 128, 255, 127, 254, 3}));
    cube_dongle::OptionalInput input;
    cube_dongle::PointerReport report;
    assert(input.Trackballs() == 0 && input.Encoders() == 0);
    assert(!input.Motion(0, 1, 2, 0));
    assert(!input.Rotate(0, 1));
    input.Hardware(1, 1);
    assert(!input.Configure({0, false, false, false}));
    assert(input.Configure({800, true, false, false}));
    assert(input.Motion(0, -32768, 32767, 1));
    assert(input.Pop(report));
    assert(report.x == 32767 && report.y == 32767 && report.buttons == 1);
    assert(!input.Motion(1, 0, 0, 0));
    assert(!input.Motion(0, 0, 0, 32));
    assert(input.Rotate(0, -2, true));
    assert(input.Pop(report) && report.pan == -2 && report.wheel == 0 && report.buttons == 1);
    for (unsigned i = 0; i < 64; ++i)
        assert(input.Rotate(0, 1));
    assert(!input.Rotate(0, 1));
    input.Reset();
    assert(!input.Pop(report));
    input.Hardware(0, 0);
    assert(!input.Rotate(0, 1));

    // Independent pointers must not release each other's buttons. Encoder
    // scrolling also preserves held buttons, including identical deltas.
    input.Hardware(2, 1);
    assert(input.Motion(0, 0, 0, 1));
    assert(input.Motion(1, 0, 0, 2));
    assert(input.Motion(0, 0, 0, 0));
    assert(input.Pop(report) && report.buttons == 1);
    assert(input.Pop(report) && report.buttons == 3);
    assert(input.Pop(report) && report.buttons == 2);
    for (unsigned i = 0; i < 64; ++i)
        assert(input.Rotate(0, 1));
    // A rejected event must not silently update button state.
    assert(!input.Motion(1, 0, 0, 0));
    for (unsigned i = 0; i < 64; ++i)
        assert(input.Pop(report) && report.buttons == 2 && report.wheel == 1);
    assert(!input.Pop(report));
    assert(input.Rotate(0, 1));
    assert(input.Pop(report) && report.buttons == 2);
    input.Reset();
    assert(input.Rotate(0, 1));
    assert(input.Pop(report) && report.buttons == 0);
}

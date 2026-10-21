#include "host_cycle.h"
#include <cassert>

int main() {
    using cube_dongle::NextHostCycleSlot;
    for (unsigned previous = 0; previous < 5; ++previous)
        assert(NextHostCycleSlot(true, previous) == 1);
    for (unsigned profile = 0; profile < 4; ++profile)
        assert(NextHostCycleSlot(false, profile) == profile + 2);
    assert(NextHostCycleSlot(false, 4) == 0);
    assert(NextHostCycleSlot(false, 9) == 1);
    unsigned slot = 0;
    for (unsigned step = 1; step <= 12; ++step) {
        slot = NextHostCycleSlot(slot == 0, slot ? slot - 1 : 0);
        assert(slot == step % 6);
    }
}

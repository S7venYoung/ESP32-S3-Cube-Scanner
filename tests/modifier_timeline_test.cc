#include "../main/scanner/modifier_timeline.h"
#include <cassert>
int main() {
    ModifierTimeline t;
    assert(t.Get(0) == 0);
    t.Observe(0, 0, 10);
    assert(t.Observe(1, 4, 20));  // Canonical Cmd must not wait for fight expiry.
    assert(t.Get(20) == 4);
    assert(!t.Observe(0, 0, 30));  // Repeated old fight zero cannot overwrite Cmd.
    assert(t.Get(30) == 4);
    t.Observe(0, 4, 40);
    assert(t.Observe(1, 0, 50));
    t.Observe(0, 4, 60);  // Old held state must not undo a newer release.
    assert(t.Get(60) == 0);
    t.Observe(0, 0, 70);
    assert(t.Observe(0, 4, 80));
    assert(t.Get(3000080) == 4 && t.Get(3000081) == 0);
    t.Observe(1, 4, 3000090);  // A new transition recovers immediately.
    assert(t.Get(3000090) == 4);
    t.Observe(1, 0, 3000100);
    assert(t.Get(3000100) == 0);
    ModifierTimeline held;
    held.Observe(0, 4, 100);
    held.Observe(0, 4, 2000100);
    assert(held.Get(4000100) == 4);
    assert(held.Get(5000101) == 0);
    held.Observe(0, 4, 5000200);  // Accepted new sequence is a new HID sample.
    assert(held.Get(5000200) == 4);
    ModifierTimeline canonical;
    canonical.Observe(1, 4, 100);
    canonical.Observe(1, 4, 4000000);  // Canonical repeats are not sequenced.
    assert(canonical.Get(4000000) == 0);
}

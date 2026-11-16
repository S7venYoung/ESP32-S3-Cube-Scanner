#include "../main/scanner/modifier_timeline.h"
#include <cassert>
int main() {
    ModifierTimeline t;
    assert(t.Get(0) == 0);
    t.Observe(0, 0, 10);
    assert(t.Observe(1, 4, 20));
    assert(!t.Observe(0, 0, 30));
    assert(t.Get(30) == 4);
    assert(t.Observe(1, 0, 50));
    assert(!t.Observe(0, 4, 60));
    assert(t.Get(60) == 0);
    // Even an ABCE transition cannot undo a recent canonical release.
    t.Observe(0, 0, 70);
    t.Observe(0, 4, 80);
    assert(t.Get(80) == 0);
    // ABCE-only keyboards recover after the canonical stream disappears.
    assert(t.Observe(0, 4, 500051));
    assert(t.Get(500051) == 4);
    assert(t.Get(3500052) == 0);
    // Repeated complete ABCD samples keep a held key visible.
    ModifierTimeline canonical;
    canonical.Observe(1, 4, 100);
    canonical.Observe(1, 4, 2000100);
    assert(canonical.Get(4000100) == 4);
    assert(canonical.Get(5000101) == 0);
    canonical.Observe(1, 4, 5000200);
    assert(canonical.Get(5000200) == 4);
    canonical.Observe(1, 0, 5000300);
    assert(canonical.Get(5000300) == 0);
    ModifierTimeline fight;
    fight.Observe(0, 4, 10);
    fight.Observe(0, 4, 2000010);
    assert(fight.Get(4000010) == 4);
    fight.Observe(0, 0, 4000020);
    assert(fight.Get(4000020) == 0);
}

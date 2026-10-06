#include "../main/scanner/codex_metrics.h"
#include <cassert>
#include <iostream>
int main() {
    CodexMetrics value;
    assert(ParseCodexFrame("CODEX2 80 64 7000000 5 120", value));
    assert(value.left == 80 && value.week_left == 64 && value.tokens == 7000000);
    assert(ParseCodexFrame("CODEX2 -1 -1 -1 86400 120", value));
    assert(ParseCodexFrame("CODEX 86 1234", value) && value.week_left == -1);
    assert(ParseCodexFrame("CODEX 86 1234 90", value) && value.week_left == 90);
    assert(ParseCodexFrame("CODEX2 0 100 4294967295 0 30", value));
    for (auto* bad : {"", "PING", "CODEX2 1 2 3", "CODEX2 101 2 3 0 120",
         "CODEX2 -2 2 3 0 120", "CODEX2 1 2 -2 0 120", "CODEX2 1 2 4294967296 0 120",
         "CODEX2 1 2 3 -1 120", "CODEX2 1 2 3 0 -1", "CODEX2 1 2 3 0 29",
         "CODEX2 1 2 3 0 120 trailing", "CODEX 1 2 xyz", "CODEX 1 2 3 4",
         "CODEX2 1 2 99999999999999999999999999 0 120"}) {
        assert(!ParseCodexFrame(bad, value));
    }
    std::cout << "Codex protocol tests passed\n";
}

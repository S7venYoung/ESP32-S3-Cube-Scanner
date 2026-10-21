#include "../../main/scanner/codex_metrics.h"
#include <cassert>
#include <string>

int main() {
    CodexMetrics value;
    char text[16];
    assert(ParseCodexFrame("CODEX3 68 65 68200000 2 120 1577836800 28800", value));
    FormatCodexReset(value, true, text, sizeof(text));
    assert(std::string(text) == "08:00");
    value.utc_offset_seconds = -18000;
    FormatCodexReset(value, true, text, sizeof(text));
    assert(std::string(text) == "19:00");
    FormatCodexReset(value, false, text, sizeof(text));
    assert(std::string(text) == "--:--");
    assert(ParseCodexFrame("CODEX2 68 65 68200000 2 120", value));
    assert(value.reset_at == -1);
    FormatCodexReset(value, true, text, sizeof(text));
    assert(std::string(text) == "--:--");
    assert(ParseCodexFrame("CODEX3 -1 -1 -1 0 120 -1 0", value));
    for (const char* bad : {"CODEX3 1 2 3 0 120", "CODEX3 1 2 3 0 120 1 0",
                           "CODEX3 1 2 3 0 120 1577836800 50401",
                           "CODEX3 1 2 3 0 120 -2 0", "CODEX3 1 2 3 0 120 -1 0 extra"})
        assert(!ParseCodexFrame(bad, value));
}

#include "../main/scanner/scanner_policy.h"
#include <cassert>
int main() {
    auto timing = scanner_policy::Timing(false);
    assert(timing.window == timing.interval && timing.window == 0x30);
    timing = scanner_policy::Timing(true);
    assert(timing.window * 4 == timing.interval);
    timing = scanner_policy::Timing(false, true);
    assert(timing.window * 8 == timing.interval);
    const auto hot_voice = scanner_policy::Timing(true, true);
    assert(hot_voice.window == timing.window && hot_voice.interval == timing.interval);
    assert(scanner_policy::kScanSliceMs == 1000 && scanner_policy::kScanRestartMs == 1);
    scanner_policy::Names names;
    std::string out;
    assert(!names.Resolve("A", "", "Velvet46", true, 0, out));
    assert(!names.Resolve("A", "CUBE-FIGHT", "Velvet46", true, 0, out));
    assert(!names.Resolve("B", "Velvet46-other", "Velvet46", true, 1, out));
    assert(names.Resolve("A", "Velvet46", "Velvet46", false, 2, out));
    assert(names.Resolve("A", "CUBE-FIGHT", "Velvet46", false, 3, out) && out == "Velvet46");
    assert(names.Resolve("A", "CUBE-FIGHT", "Velvet46", true, 4, out) && out == "Velvet46");
    assert(!names.Resolve("B", "CUBE-FIGHT", "Velvet46", true, 5, out));
    assert(names.Resolve("A", "", "Velvet46", true, 30000000, out) && out == "Velvet46");
    assert(!names.Resolve("B", "", "Velvet46", true, 30000000, out));
    assert(!names.Resolve("A", "Other", "Velvet46", true, 31000000, out));
    assert(!names.Resolve("A", "", "Velvet46", true, 32000000, out));
    assert(names.Resolve("A", "Velvet46", "Velvet46", true, 33000000, out));
    assert(!names.Resolve("A", "", "Velvet46", true, 214000000, out));
    assert(names.Resolve("C", "", "", true, 214000001, out));
    uint8_t packet[28] = {27, 0xff, 255, 255, 0xab, 0xcd, 0x22, 72, 0, 0x18, 1, 16, 1};
    assert(scanner_policy::HasStatus(packet, sizeof(packet)));
    packet[12] = 2;
    assert(!scanner_policy::HasStatus(packet, sizeof(packet)));
    packet[12] = 1;
    packet[0] = 29;
    assert(!scanner_policy::HasStatus(packet, sizeof(packet)));
    assert(!scanner_policy::HasStatus(nullptr, 28));
    scanner_policy::Retry retry;
    for (unsigned expected : {1000u, 2000u, 4000u, 8000u, 16000u, 30000u})
        assert(retry.FailureDelayMs() == expected);
    assert(retry.FailureDelayMs() == 0);
    retry.Healthy();
    assert(retry.FailureDelayMs() == 1000);
}

#include "keyboard_dashboard.h"
#include <cassert>
#include <string>
#include "split_relay.h"
int main() {
    using namespace cube_dongle;
    std::array<uint8_t, DASH_DATA_SIZE> sample{};
    const char* frame = "HOST1 013f0118465240007b02b300000154240000";
    assert(ParseHostMetricsFrame(frame, sample));
    assert(sample[3] == 24 && sample[4] == 70 && dash_u16(sample.data() + 8) == 635);
    const auto previous = sample;
    for (const auto* bad : {"", "HOST1 00", "HOST1 013f0118465240007b02b30000015424000z",
                            "HOST1 013f0118465240007b02b300000154240000extra"}) {
        assert(!ParseHostMetricsFrame(bad, sample));
        assert(sample == previous);
    }
    assert(!ParseHostMetricsFrame(nullptr, sample));
    KeyboardDashboard store;
    auto packet = store.Packet(DASH_OUTPUT_NONE, 0, 0);
    assert(packet[0] == 2 && packet[1] == 1 && packet[4] == 1 && packet[5] == 0);
    assert(store.Update(sample.data(), sample.size(), 1000));
    for (unsigned profile = 1; profile <= 5; ++profile) {
        packet = store.Packet(DASH_OUTPUT_BLE, profile, 2000);
        assert(dash_theme_packet_valid(packet.data(), packet.size()));
        assert(packet[2] == 2 && packet[3] == profile && packet[7] == 24);
        assert(dash_u32(packet.data() + 18) == 9299);
        for (unsigned mtu : {23, 64, 247}) {
            uint8_t sequence = 125;
            const auto chunks =
                SplitRelay::Encode("vldash", {packet.begin(), packet.end()}, mtu, sequence);
            SplitRelay receiver;
            std::string name;
            std::vector<uint8_t> body;
            for (size_t i = 0; i < chunks.size(); ++i)
                assert(receiver.Feed(chunks[i].data(), chunks[i].size(), name, body) ==
                       (i + 1 == chunks.size()));
            assert(name == "vldash" && body == std::vector<uint8_t>(packet.begin(), packet.end()));
        }
    }
    assert(store.Packet(DASH_OUTPUT_USB, 0, 90999)[5] == 63);
    assert(store.Packet(DASH_OUTPUT_USB, 0, 91000)[5] == 0);
    auto bad = sample;
    bad[3] = 101;
    assert(!store.Update(bad.data(), bad.size(), 91001));
    assert(store.Packet(1, 0, 91002)[5] == 0);  // Bad frames cannot refresh the TTL.
    packet = store.Packet(DASH_OUTPUT_BLE, 6, 2000);
    assert(packet[2] == 0 && packet[3] == 0);
    bad = sample;
    bad[1] = DASH_TEMP;
    bad[8] = 0x0c;
    bad[9] = 0xfe;  // -50 C
    assert(store.Update(bad.data(), bad.size(), 1));
    bad[8] = 0x0b;
    assert(!store.Update(bad.data(), bad.size(), 2));
    return 0;
}

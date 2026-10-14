#include <cassert>
#include <iostream>
#include "runtime_combos.h"
#include "runtime_macros.h"
#include "studio_diagnostics.h"
#include "studio_watchdog.h"

using namespace cube_dongle::studio;
Bytes Request(uint32_t tag, const Bytes& arguments = {}) {
    Bytes result;
    Message(result, tag, arguments);
    return result;
}
int main(int argc, char**) {
    if (argc > 1) {
        RuntimeMacros macros;
        RuntimeCombos combos;
        Bytes combo_stored;
        Diagnostics diagnostics;
        WatchdogLog log;
        Rpc session;
        Bytes stored;
        bool fail = false;
        log.Boot(16);
        session.custom.push_back({"cormoran__runtime_macro", "https://example.org/macro",
                                  [&](const Bytes& payload, bool unlocked) {
                                      return macros.Handle(payload, unlocked,
                                                           [&](const Bytes& bytes) {
                                                               if (fail)
                                                                   return false;
                                                               stored = bytes;
                                                               return true;
                                                           });
                                  }});
        session.custom.push_back({"cormoran__kscan_diagnostics", "https://example.org/diagnostics",
                                  [&](const Bytes& payload, bool unlocked) {
                                      return diagnostics.Handle(payload, unlocked, 123);
                                  }});
        session.custom.push_back({"cormoran__watchdog", "https://example.org/watchdog",
                                  [&](const Bytes& payload, bool unlocked) {
                                      return log.Handle(payload, unlocked,
                                                        [&](const Bytes&) { return !fail; });
                                  }});
        session.custom.push_back({"cormoran__runtime_combo", "https://example.org/combo",
                                  [&](const Bytes& payload, bool unlocked) {
                                      return combos.Handle(payload, unlocked,
                                                           [&](const Bytes& bytes) {
                                                               if (fail)
                                                                   return false;
                                                               combo_stored = bytes;
                                                               return true;
                                                           });
                                  }});
        std::string line;
        while (std::getline(std::cin, line)) {
            Bytes response;
            if (line == "unlock")
                response = session.LockState(true);
            else if (line == "fail-save")
                fail = true;
            else if (line == "reload") {
                macros = RuntimeMacros{};
                macros.Load(stored);
                combos = RuntimeCombos{};
                combos.Load(combo_stored);
            } else {
                Bytes request;
                if (line.size() <= 8192 && line.size() % 2 == 0) {
                    for (size_t i = 0; i < line.size(); i += 2)
                        request.push_back(std::stoul(line.substr(i, 2), nullptr, 16));
                    response = session.Handle(request, [](const Bytes&) { return false; });
                }
            }
            for (uint8_t b : Frame(response)) {
                const char* hex = "0123456789abcdef";
                std::cout << hex[b >> 4] << hex[b & 15];
            }
            std::cout << std::endl;
        }
        return 0;
    }
    Rpc rpc;
    rpc.custom.push_back({"test__secured", "https://example.org/", [](const Bytes&, bool unlocked) {
                              return unlocked ? Request(2) : CustomError("locked");
                          }});
    Bytes envelope;
    Number(envelope, 1, 91);
    Message(envelope, 100, Request(1));
    // Reader stores pointers: hold response bytes throughout nested inspection.
    auto bytes = rpc.Handle(envelope, [](const Bytes&) { return false; });
    Reader outer(bytes);
    const auto* rr = outer.Get(1);
    assert(rr);
    Reader request_response(rr->data, rr->size);
    assert(request_response.Value(1) == 91);
    const auto* custom = request_response.Get(100);
    assert(custom && custom->wire == 2);
    Bytes args;
    Number(args, 1, 0);
    Message(args, 2, Request(1));
    envelope.clear();
    Number(envelope, 1, 92);
    Message(envelope, 100, Request(2, args));
    assert(!rpc.Handle(envelope, [](const Bytes&) { return false; }).empty());
    rpc.LockState(true);
    assert(!rpc.Handle(envelope, [](const Bytes&) { return false; }).empty());
    Diagnostics diagnostics;
    cube_dongle::SplitState::Snapshot positions{};
    positions[0] = 1;
    diagnostics.Sample(positions, 100);
    positions[0] = 0;
    diagnostics.Sample(positions, 110);
    positions[0] = 1;
    diagnostics.Sample(positions, 112);
    assert(Reader(diagnostics.Handle(Request(1), false, 112)).valid);
    auto locked = diagnostics.Handle(Request(7), false, 112);
    assert(Reader(locked).Get(1));
    auto reset = diagnostics.Handle(Request(7), true, 112);
    assert(Reader(reset).Get(8));
    assert(Reader(diagnostics.Handle(Request(3), false, 112)).Get(1));
    WatchdogLog log;
    log.Boot(16);
    auto record = log.Export();
    WatchdogLog restored;
    assert(restored.Load(record));
    record.back() ^= 1;
    assert(!restored.Load(record));
    assert(Reader(log.Handle(Request(1), false, [](const Bytes&) { return false; })).Get(2));
    Bytes all;
    Number(all, 2, 1);
    assert(Reader(log.Handle(Request(3, all), false, [](const Bytes&) { return true; })).Get(1));
    assert(Reader(log.Handle(Request(3, all), true, [](const Bytes&) { return false; })).Get(1));
    assert(Reader(log.Handle(Request(3, all), true, [](const Bytes&) { return true; })).Get(4));
    assert(Reader(log.Handle(Request(4), true, [](const Bytes&) { return true; })).Get(1));
}

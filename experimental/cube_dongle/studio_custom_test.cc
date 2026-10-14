#include <cassert>
#include "studio_diagnostics.h"
#include "studio_watchdog.h"

using namespace cube_dongle::studio;
Bytes Request(uint32_t tag, const Bytes& arguments = {}) {
    Bytes result;
    Message(result, tag, arguments);
    return result;
}
int main() {
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

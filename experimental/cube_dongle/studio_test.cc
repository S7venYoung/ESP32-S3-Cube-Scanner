#include <cassert>
#include <iostream>
#include <random>
#include "studio_protocol.h"
using namespace cube_dongle;
using namespace cube_dongle::studio;

Bytes Request(uint32_t subsystem, uint32_t operation, const Bytes* data = nullptr) {
    Bytes body, request;
    if (data)
        Message(body, operation, *data);
    else
        Number(body, operation, 1);
    Number(request, 1, 7);
    Message(request, subsystem, body);
    return request;
}
Bytes ResponseBody(const Bytes& response, uint32_t subsystem) {
    Reader outer(response);
    assert(outer.valid);
    auto* rr = outer.Get(1);
    assert(rr && rr->wire == 2);
    Reader body(rr->data, rr->size);
    assert(body.valid && body.Value(1) == 7);
    auto* sub = body.Get(subsystem);
    assert(sub && sub->wire == 2);
    return Bytes(sub->data, sub->data + sub->size);
}

int main(int argc, char**) {
    if (argc > 1) {
        Rpc session;
        Bytes stored;
        bool fail = false;
        std::string line;
        auto output = [](const Bytes& payload) {
            for (auto b : Frame(payload)) {
                const char* hex = "0123456789abcdef";
                std::cout << hex[b >> 4] << hex[b & 15];
            }
            std::cout << std::endl;
        };
        while (std::getline(std::cin, line)) {
            if (line == "unlock") {
                output(session.LockState(true));
                continue;
            }
            if (line == "reload") {
                session = Rpc{};
                if (!stored.empty())
                    session.Load(stored);
                output({});
                continue;
            }
            if (line == "fail-save") {
                fail = true;
                output({});
                continue;
            }
            Bytes request;
            if (line.size() > 8192 || line.size() % 2) {
                output({});
                continue;
            }
            for (size_t i = 0; i < line.size(); i += 2)
                request.push_back(std::stoul(line.substr(i, 2), nullptr, 16));
            output(session.Handle(request, [&](const Bytes& value) {
                if (fail)
                    return false;
                stored = value;
                return true;
            }));
        }
        return 0;
    }
    Rpc rpc;
    unsigned saved = 0;
    Bytes persisted;
    auto save = [&](const Bytes& bytes) {
        ++saved;
        persisted = bytes;
        return true;
    };
    auto response = rpc.Handle(Request(3, 1), save);
    const auto device_body = ResponseBody(response, 3);
    Reader info(device_body);
    assert(info.valid && info.Get(1));
    auto record = rpc.model.Export();
    Model loaded;
    assert(loaded.Load(record));
    assert(loaded.Export() == record);
    Bytes legacy{'C', 'V', 'S', 1};
    legacy.insert(legacy.end(), record.begin() + 9, record.begin() + 9 + 3 * (17 + 61 * 4));
    const uint32_t legacy_hash = Model::Hash(legacy.data(), legacy.size());
    for (unsigned i = 0; i < 4; ++i)
        legacy.push_back(legacy_hash >> (i * 8));
    Model migrated;
    assert(migrated.Load(legacy) && migrated.count == 3);
    assert(migrated.names[0] == "BASE" && migrated.map[0][26].value == 4);
    auto corrupt = record;
    corrupt[13] ^= 1;
    assert(!loaded.Load(corrupt));
    assert(loaded.Export() == record);
    for (size_t l = 0; l < 3; ++l)
        for (const auto& binding : rpc.model.map[l]) {
            Binding other;
            assert(BindingFromWire(Behavior(binding) * 2, Param1(binding), Param2(binding), other));
            assert(binding.kind == other.kind && binding.value == other.value &&
                   binding.modifiers == other.modifiers);
        }
    Binding invalid;
    assert(!BindingFromWire(2, 0xffffffffffffffffULL, 0, invalid));
    assert(!BindingFromWire(4, 4, 0, invalid));
    assert(!BindingFromWire(10, 1, 0, invalid));
    Bytes binding, edit;
    Signed(binding, 1, kKp);
    Number(binding, 2, 0x70005);
    Number(edit, 1, 0);
    Number(edit, 2, 26);
    Message(edit, 3, binding);
    auto denied = ResponseBody(rpc.Handle(Request(5, 2, &edit), save), 2);
    Reader denied_reader(denied);
    assert(denied_reader.Value(2) == 1 && !saved);
    rpc.LockState(true);
    auto changed = ResponseBody(rpc.Handle(Request(5, 2, &edit), save), 5);
    Reader changed_reader(changed);
    assert(changed_reader.Value(2) == 0);
    assert(rpc.model.map[0][26].value == 5 && rpc.Dirty());
    rpc.Handle(Request(5, 4), save);
    assert(saved == 1 && !rpc.Dirty());
    Rpc restarted;
    restarted.Load(persisted);
    assert(restarted.model.map[0][26].value == 5 && !restarted.Dirty());
    Number(binding, 3, 1);
    Bytes badedit;
    Number(badedit, 1, 0);
    Number(badedit, 2, 26);
    Message(badedit, 3, binding);
    rpc.Handle(Request(5, 2, &badedit), save);
    assert(rpc.model.map[0][26].value == 5);
    Bytes props;
    Number(props, 1, 1);
    Text(props, 2, "NAV");
    rpc.Handle(Request(5, 12, &props), save);
    assert(rpc.model.names[1] == "NAV" && rpc.Dirty());
    auto failed = ResponseBody(rpc.Handle(Request(5, 4), [](const Bytes&) { return false; }), 5);
    Reader failed_reader(failed);
    assert(failed_reader.valid && rpc.Dirty());
    rpc.Handle(Request(5, 5), save);
    assert(rpc.model.names[1] == "NVA" && !rpc.Dirty());
    KeyboardState<61, 4> keyboard(VelvetStudioKeymap());
    SplitState::Snapshot snapshot{};
    snapshot[26 / 8] = 1u << (26 % 8);
    keyboard.Apply(snapshot);
    assert(keyboard.Report()[2] == 4);
    keyboard.SetKeymap(restarted.model.map);
    keyboard.Apply(snapshot);
    assert(keyboard.Report()[2] == 4);
    keyboard.Apply({});
    keyboard.Apply(snapshot);
    assert(keyboard.Report()[2] == 5);
    Framer framer;
    Bytes specials{0xab, 0xac, 0xad, 0, 255};
    unsigned received = 0;
    for (auto b : Frame(specials))
        assert(framer.Feed(b, [&](const Bytes& value) {
            assert(value == specials);
            ++received;
        }));
    assert(received == 1 && !framer.Active());
    assert(!framer.Feed('P', [](const Bytes&) {}));
    framer.Feed(0xab, [](const Bytes&) {});
    for (size_t i = 0; i < kLimit + 1; ++i)
        framer.Feed(0, [](const Bytes&) {});
    framer.Feed(0xad, [&](const Bytes&) { ++received; });
    assert(received == 1);
    rpc.LockState(false);
    std::mt19937 random(42);
    for (unsigned i = 0; i < 5000; ++i) {
        Bytes fuzz(random() % 128);
        for (auto& byte : fuzz)
            byte = random();
        rpc.Handle(fuzz, save);
        for (auto b : fuzz)
            framer.Feed(b, [](const Bytes&) {});
    }
    assert(saved == 1);
    assert(rpc.model.Keymap().size() < kLimit && PhysicalLayouts().size() < kLimit);
    std::cout << "Studio protocol, persistence, input latching and framing tests passed\n";
}

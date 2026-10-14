#include <cassert>
#include <random>
#include "combo_engine.h"
using namespace cube_dongle;
using namespace cube_dongle::studio;
SplitState::Snapshot Positions(std::initializer_list<size_t> positions) {
    SplitState::Snapshot out{};
    for (auto p : positions)
        out[p / 8] |= 1u << (p % 8);
    return out;
}
ComboModel Basic() {
    ComboModel model;
    model.entries[0].keys = {0, 1};
    model.entries[0].binding = {Kind::Key, 40, 0};
    model.entries[0].enabled = true;
    return model;
}
Bytes Operation(unsigned tag, const Bytes& body = {}) {
    Bytes out;
    Message(out, tag, body);
    return out;
}
int main() {
    auto model = Basic();
    auto saved = model.Export();
    ComboModel restored;
    assert(restored.Load(saved));
    assert(restored.entries[0].keys == model.entries[0].keys);
    saved.back() ^= 1;
    assert(!restored.Load(saved));
    ComboEngine engine;
    assert(engine.Configure(model));
    auto result = engine.Apply(Positions({0}), 100, 0);
    assert(result.filtered == Positions({}) && engine.Pending());
    result = engine.Apply(Positions({0, 1}), 120, 0);
    assert(result.report.keyboard[2] == 40 && result.filtered == Positions({}));
    assert(!engine.Pending());
    assert(!engine.Configure(model));  // Do not replace active chord definitions.
    result = engine.Apply(Positions({1}), 130, 0);
    assert(result.report.Neutral() && result.filtered == Positions({}));
    engine.Apply({}, 140, 0);
    assert(engine.Neutral());
    result = engine.Apply(Positions({0}), 200, 0);
    result = engine.Apply(Positions({0}), 240, 0);
    assert(result.filtered == Positions({0}));
    result = engine.Apply({}, 250, 0);
    assert(result.filtered == Positions({}) && result.taps == Positions({}));
    engine.Apply(Positions({0}), 300, 0);
    result = engine.Apply({}, 305, 0);
    assert(result.taps == Positions({0}) && result.filtered == Positions({}));
    model.slow_release = true;
    assert(engine.Configure(model));
    engine.Apply(Positions({0, 1}), 400, 0);
    result = engine.Apply(Positions({1}), 410, 0);
    assert(result.report.keyboard[2] == 40);
    result = engine.Apply({}, 420, 0);
    assert(result.report.Neutral());
    model.entries[1] = model.entries[0];
    model.entries[1].keys = {0, 1, 2};
    model.entries[1].binding.value = 41;
    assert(engine.Configure(model));
    result = engine.Apply(Positions({0, 1}), 500, 0);
    assert(result.report.Neutral());  // Wait for the possible larger chord.
    result = engine.Apply(Positions({0, 1, 2}), 520, 0);
    assert(result.report.keyboard[2] == 41);
    engine.Apply({}, 530, 0);
    engine.Apply(Positions({0, 1}), 600, 0);
    result = engine.Apply(Positions({0, 1}), 640, 0);
    assert(result.report.keyboard[2] == 40);
    engine.Apply({}, 650, 0);
    model = Basic();
    model.entries[0].layers = 2;
    assert(engine.Configure(model));
    result = engine.Apply(Positions({0, 1}), 700, 0);
    assert(result.filtered == Positions({0, 1}) && result.report.Neutral());
    engine.Apply({}, 710, 0);
    result = engine.Apply(Positions({0, 1}), 720, 1);
    assert(result.report.keyboard[2] == 40);
    engine.Cancel();
    result = engine.Apply(Positions({0, 1}), 730, 1);
    assert(result.filtered == Positions({}) && result.report.Neutral());
    engine.Apply({}, 740, 1);
    result = engine.Apply(Positions({0, 1}), 750, 1);
    assert(result.report.keyboard[2] == 40);
    engine.Apply({}, 760, 1);
    model = Basic();
    model.prior_idle = 100;
    assert(engine.Configure(model));
    engine.Apply(Positions({3}), 800, 0);
    engine.Apply({}, 810, 0);
    result = engine.Apply(Positions({0, 1}), 820, 0);
    assert(result.filtered == Positions({0, 1}) && result.report.Neutral());
    engine.Apply({}, 830, 0);
    result = engine.Apply(Positions({0, 1}), 1000, 0);
    assert(result.report.keyboard[2] == 40);
    engine.Apply({}, 1010, 0);
    model.prior_idle = 0;
    model.entries[0].binding = {Kind::RuntimeMacro, 2, 0};
    assert(engine.Configure(model));
    result = engine.Apply(Positions({0, 1}), 1100, 0);
    assert(result.macro_mask == 4);
    result = engine.Apply(Positions({0, 1}), 1105, 0);
    assert(result.macro_mask == 0);
    engine.Apply({}, 1110, 0);
    model.entries[0].binding = {Kind::MomentaryLayer, 3, 0};
    assert(engine.Configure(model));
    result = engine.Apply(Positions({0, 1}), 1200, 0);
    assert(result.layers == 8);
    RuntimeCombos combos;
    Bytes args, binding;
    Number(args, 1, 0);
    const uint8_t keys[] = {0, 1};
    Data(args, 2, keys, sizeof(keys));
    Number(binding, 1, 1);
    Number(binding, 2, 0x70028);
    Message(args, 3, binding);
    Number(args, 7, 1);
    const auto request = Operation(3, args);
    auto response = combos.Handle(request, false, [](const Bytes&) { return true; });
    assert(Reader(response).Get(1));
    response = combos.Handle(request, true, [](const Bytes&) { return true; });
    assert(Reader(response).Get(4));
    const auto before = combos.model.Export();
    response = combos.Handle(Operation(9), true, [](const Bytes&) { return false; });
    assert(Reader(response).Get(1) && combos.model.Export() == before);
    ReportFrame a, b;
    a.keyboard[2] = 4;
    b.keyboard[0] = 2;
    b.keyboard[2] = 5;
    auto combined = MergeReports(a, b);
    assert(combined.keyboard[0] == 2 && combined.keyboard[2] == 4 && combined.keyboard[3] == 5);
    ComboEngine wrap;
    assert(wrap.Configure(Basic()));
    wrap.Apply(Positions({0}), 0xfffffff0u, 0);
    result = wrap.Apply(Positions({0}), 0x18, 0);
    assert(result.filtered == Positions({0}));
    std::mt19937 random(19);
    for (unsigned i = 0; i < 5000; ++i) {
        Bytes fuzz(random() % 1100);
        for (auto& byte : fuzz)
            byte = random();
        if (fuzz.size() >= 137) {
            std::memcpy(fuzz.data(), "CCB\1", 4);
            const auto hash = Model::Hash(fuzz.data(), fuzz.size() - 4);
            for (unsigned j = 0; j < 4; ++j)
                fuzz[fuzz.size() - 4 + j] = hash >> (j * 8);
        }
        ComboModel candidate;
        if (candidate.Load(fuzz)) {
            ComboModel roundtrip;
            assert(roundtrip.Load(candidate.Export()));
        }
    }
    ComboEngine stress;
    assert(stress.Configure(Basic()));
    uint32_t now = 0xffffff00u;
    for (unsigned i = 0; i < 10000; ++i) {
        SplitState::Snapshot raw{};
        raw[0] = random() & 15;
        now += random() % 100;
        result = stress.Apply(raw, now, 0);
        for (size_t j = 0; j < raw.size(); ++j)
            assert(!(result.filtered[j] & ~raw[j]));
        if (raw == SplitState::Snapshot{})
            assert(result.report.Neutral());
    }
}

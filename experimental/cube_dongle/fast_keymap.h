#pragma once
#include "studio_diagnostics.h"
namespace cube_dongle::studio {
// All fingerprints are opaque validators, not IDs or hashes of the web bundle.
// Full details are served for cache miss repair; 32 bits avoid truncation aliases.
inline Bytes FastKeymap(const Bytes& payload, const Rpc& rpc) {
    auto error = [](unsigned code) {
        Bytes body, out;
        Number(body, 1, code);
        Message(out, 1, body);
        return out;
    };
    Reader request(payload);
    const auto* op = request.Operation();
    if (!request.valid || !op)
        return error(0);
    const auto hash = [](const Bytes& bytes) { return Model::Hash(bytes.data(), bytes.size()); };
    const auto& model = rpc.model;
    const Model stock, saved = rpc.SavedModel();
    auto equal = [](const Binding& a, const Binding& b) {
        return a.kind == b.kind && a.value == b.value && a.modifiers == b.modifiers;
    };
    const char* aliases[] = {"", "kp", "mo", "trans", "none", "bt", "runtime_macro", "mt", "lt"};
    auto details = [&](unsigned id, bool alias, bool exclude_name) {
        const auto official = Rpc::BehaviorDetails(id);
        Reader source(official.data(), official.size(), {3});
        Bytes body;
        if (!exclude_name) {
            const auto* name = source.Get(2);
            if (name)
                Data(body, 1, name->data, name->size);
        }
        if (alias)
            Text(body, 2, aliases[id]);
        for (const auto& meta : source.Repeated(3))
            Data(body, 3, meta.data, meta.size);
        return body;
    };
    const auto physical = PhysicalLayouts();
    Reader official_layouts(physical.data(), physical.size(), {2});
    const auto* layout_field = official_layouts.Get(2);
    if (!layout_field)
        return error(0);
    Reader layout(layout_field->data, layout_field->size, {2});
    auto geometry = [&](bool keys) {
        Bytes out;
        const auto* name = layout.Get(1);
        if (name)
            Data(out, 1, name->data, name->size);
        Number(out, 2, hash(physical));
        if (keys)
            for (const auto& key : layout.Repeated(2))
                Data(out, 3, key.data, key.size);
        return out;
    };
    Bytes body, response;
    Reader args(op->data, op->size, {1});
    if (op->tag == 1 && op->wire == 0) {
        Bytes behaviors;
        for (unsigned id = 1; id <= 8; ++id)
            Message(behaviors, id, details(id, true, false));
        Number(body, 1, 1);
        Number(body, 2, hash(behaviors));
        Number(body, 3, hash(stock.Export()));
        Number(body, 4, hash(model.Export()));
        Number(body, 5, hash(physical));
        Number(body, 6, 8);
        Number(body, 7, 16);
        Number(body, 8, 4 - model.count);
        Number(body, 9, 0);
        Number(body, 11, 32);
        for (size_t n = 0; n < model.count; ++n) {
            const size_t id = model.order[n];
            Bytes layer;
            Number(layer, 1, id);
            Text(layer, 2, model.names[id]);
            Number(layer, 3, 61);
            Number(layer, 4, hash(model.Layer(id)));
            Number(layer, 5, hash(stock.Layer(id)));
            unsigned edited = 0;
            for (size_t p = 0; p < 61; ++p)
                edited += !equal(model.map[id][p], stock.map[id][p]);
            Number(layer, 6, edited);
            Message(body, 10, layer);
        }
        Message(response, 2, body);
        return response;
    }
    if (op->tag == 10 && op->wire == 0) {
        for (size_t n = 0; n < model.count; ++n) {
            const auto id = model.order[n];
            Bytes positions;
            for (size_t p = 0; p < 61; ++p)
                if (!equal(model.map[id][p], saved.map[id][p]))
                    Number(positions, 2, p);
            if (!positions.empty()) {
                Number(positions, 1, id);
                Message(body, 1, positions);
            }
        }
        Message(response, 11, body);
        return response;
    }
    if (op->wire != 2 || !args.valid)
        return error(0);
    auto ids = [&]() {
        std::vector<uint32_t> result;
        for (const auto& field : args.Repeated(1)) {
            if (field.wire == 0 && field.value <= UINT32_MAX)
                result.push_back(field.value);
            else if (field.wire == 2) {
                size_t at = 0;
                while (at < field.size) {
                    uint32_t value = 0;
                    unsigned shift = 0;
                    for (;;) {
                        if (at == field.size || shift > 28)
                            return std::vector<uint32_t>{};
                        const auto byte = field.data[at++];
                        if (shift == 28 && byte > 15)
                            return std::vector<uint32_t>{};
                        value |= uint32_t(byte & 127) << shift;
                        if (!(byte & 128))
                            break;
                        shift += 7;
                    }
                    result.push_back(value);
                    if (result.size() > 32)
                        return std::vector<uint32_t>{};
                }
            } else
                return std::vector<uint32_t>{};
        }
        return result;
    };
    if (op->tag == 2 || op->tag == 3) {
        if (!args.Numbers(op->tag == 2 ? std::initializer_list<uint32_t>{1, 2, 3}
                                       : std::initializer_list<uint32_t>{2, 3}))
            return error(0);
        auto selected = ids();
        if (op->tag == 2) {
            if (args.Value(1) > 2)
                return error(0);
            selected = {1, 2, 3, 4, 5, 6, 7, 8};
        }
        for (const auto id : selected)
            if (id >= 1 && id <= 8) {
                Bytes entry;
                Number(entry, 1, id);
                if (op->tag == 2)
                    Number(entry, 2, hash(details(id, true, false)));
                if (op->tag == 3 || args.Value(1))
                    Message(entry, op->tag == 2 ? 3 : 2, details(id, args.Value(2), args.Value(3)));
                Message(body, 1, entry);
            }
        Message(response, op->tag + 1, body);
    } else if (op->tag >= 4 && op->tag <= 6) {
        const auto id = args.Value(1);
        if (id >= 4 || !model.Active(id))
            return error(1);
        if (op->tag != 6)
            body = (op->tag == 5 ? stock : model).Layer(id);
        else {
            Number(body, 1, id);
            for (size_t p = 0; p < 61; ++p)
                if (!equal(model.map[id][p], stock.map[id][p])) {
                    Bytes entry;
                    Number(entry, 1, p);
                    Message(entry, 2, EncodeBinding(model.map[id][p]));
                    Message(body, 2, entry);
                }
        }
        Message(response, op->tag + 1, body);
    } else if (op->tag == 7 || op->tag == 8) {
        if (args.Value(1) > (op->tag == 7 ? 1u : 0u))
            return error(3);
        Number(body, 1, 0);
        Message(body, 2, geometry(op->tag == 8 || args.Value(1)));
        Message(response, op->tag + 1, body);
    } else if (op->tag == 9) {
        for (const auto id : ids())
            if (id < 4 && model.Active(id))
                Message(body, 1, model.Layer(id));
        Message(response, 10, body);
    } else
        return error(0);
    return response;
}
}  // namespace cube_dongle::studio

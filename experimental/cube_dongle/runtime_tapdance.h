#pragma once
#include <map>
#include "combo_engine.h"

namespace cube_dongle::studio {
struct DanceAction {
    uint32_t id = 0, p1 = 0, p2 = 0;
    bool operator==(const DanceAction& b) const { return id == b.id && p1 == b.p1 && p2 == b.p2; }
    bool Valid() const {
        if (!id)
            return !p1 && !p2;
        Binding b;
        return id != kRuntimeTapDance && BindingFromWire(uint64_t(id) * 2, p1, p2, b);
    }
    Binding Decode() const {
        Binding b;
        if (id)
            BindingFromWire(uint64_t(id) * 2, p1, p2, b);
        return b;
    }
    Bytes Encode() const {
        Bytes b;
        Number(b, 1, id);
        Number(b, 2, p1);
        Number(b, 3, p2);
        return b;
    }
};
struct DanceEntry {
    std::string name;
    std::array<DanceAction, 3> actions{};
    bool operator==(const DanceEntry& b) const { return name == b.name && actions == b.actions; }
    unsigned Count() const { return actions[2].id ? 3 : actions[1].id ? 2 : actions[0].id ? 1 : 0; }
    static bool Name(const std::string& s) {
        if (s.empty() || s.size() > 24)
            return false;
        for (size_t i = 0; i < s.size();) {
            uint32_t ch = uint8_t(s[i++]);
            unsigned n = 0;
            uint32_t minimum = 0;
            if (ch < 128) {
                if (ch < 32 || ch == 127)
                    return false;
                continue;
            }
            if (ch >= 0xc2 && ch <= 0xdf) {
                n = 1;
                minimum = 0x80;
                ch &= 31;
            } else if (ch >= 0xe0 && ch <= 0xef) {
                n = 2;
                minimum = 0x800;
                ch &= 15;
            } else if (ch >= 0xf0 && ch <= 0xf4) {
                n = 3;
                minimum = 0x10000;
                ch &= 7;
            } else
                return false;
            while (n--) {
                if (i == s.size() || (uint8_t(s[i]) & 0xc0) != 0x80)
                    return false;
                ch = (ch << 6) | (uint8_t(s[i++]) & 63);
            }
            if (ch < minimum || ch > 0x10ffff || (ch >= 0xd800 && ch <= 0xdfff))
                return false;
        }
        return true;
    }
    bool Valid() const {
        return Name(name) && actions[0].Valid() && actions[1].Valid() && actions[2].Valid() &&
               (!actions[1].id || actions[0].id) && (!actions[2].id || actions[1].id);
    }
};
struct DanceModel {
    std::map<uint16_t, DanceEntry> entries;
    uint32_t next_id = 0, interval = 200;
    unsigned Used() const {
        unsigned n = 0;
        for (const auto& item : entries)
            n += 38 + item.second.name.size();
        return n;
    }
    bool Valid() const {
        if (entries.size() > 16 || Used() > 1024 || !interval || interval > 2000 || next_id > 65536)
            return false;
        for (const auto& item : entries)
            if (item.first >= next_id || !item.second.Valid())
                return false;
        return true;
    }
    Bytes Export() const {
        Bytes out{'C', 'T', 'D', 1};
        Number(out, 1, next_id);
        Number(out, 2, interval);
        for (const auto& [id, e] : entries) {
            Bytes row;
            Number(row, 1, id);
            Text(row, 2, e.name);
            for (unsigned i = 0; i < 3; i++)
                Message(row, 3 + i, e.actions[i].Encode());
            Message(out, 3, row);
        }
        uint32_t hash = Model::Hash(out.data(), out.size());
        for (unsigned i = 0; i < 4; i++)
            out.push_back(hash >> (8 * i));
        return out;
    }
    static bool Action(const Reader& r, DanceAction& out) {
        if (!r.valid || !r.Numbers({1, 2, 3}) || r.Value(1) > UINT32_MAX ||
            r.Value(2) > UINT32_MAX || r.Value(3) > UINT32_MAX)
            return false;
        DanceAction a{uint32_t(r.Value(1)), uint32_t(r.Value(2)), uint32_t(r.Value(3))};
        if (!a.Valid())
            return false;
        out = a;
        return true;
    }
    bool Load(const Bytes& bytes) {
        if (bytes.size() < 8 || bytes.size() > 2048 || std::memcmp(bytes.data(), "CTD\1", 4))
            return false;
        uint32_t hash = 0;
        for (unsigned i = 0; i < 4; i++)
            hash |= uint32_t(bytes[bytes.size() - 4 + i]) << (8 * i);
        if (hash != Model::Hash(bytes.data(), bytes.size() - 4))
            return false;
        Reader body(bytes.data() + 4, bytes.size() - 8, {3});
        if (!body.valid || !body.Numbers({1, 2}) || body.Value(1) > 65536 || body.Value(2) > 2000)
            return false;
        DanceModel next;
        next.next_id = body.Value(1);
        next.interval = body.Value(2);
        for (const auto& row : body.Repeated(3)) {
            if (row.wire != 2)
                return false;
            Reader r(row.data, row.size);
            const auto* name = r.Get(2);
            if (!r.valid || !r.Numbers({1}) || r.Value(1) > 65535 || !name || name->wire != 2)
                return false;
            const uint16_t id = r.Value(1);
            if (next.entries.count(id))
                return false;
            DanceEntry entry;
            entry.name.assign(reinterpret_cast<const char*>(name->data), name->size);
            for (unsigned i = 0; i < 3; i++) {
                const auto* a = r.Get(3 + i);
                if (!a || a->wire != 2 || !Action(Reader(a->data, a->size), entry.actions[i]))
                    return false;
            }
            next.entries[id] = std::move(entry);
        }
        if (!next.Valid())
            return false;
        *this = std::move(next);
        return true;
    }
};
class RuntimeTapDances {
public:
    DanceModel model;
    bool Load(const Bytes& bytes) {
        DanceModel next;
        if (!next.Load(bytes))
            return false;
        model = saved_ = std::move(next);
        return true;
    }
    Bytes Handle(const Bytes& payload, bool unlocked, const Rpc::Save& save) {
        Reader request(payload);
        const auto* op = request.Operation();
        if (!request.valid || !op || op->wire != 2)
            return CustomError("Invalid tapdance request");
        Reader args(op->data, op->size);
        if (!args.valid)
            return CustomError("Invalid tapdance arguments");
        if (op->tag == 1) {
            if (!args.Numbers({1}) || args.Value(1) > 65535)
                return CustomError("Invalid tapdance ID");
            return State(args.Value(1));
        }
        if (op->tag == 9)
            return Global();
        if (op->tag == 5) {
            if (!args.Numbers({1, 2}) || args.Value(1) > 65535 || args.Value(2) > 1)
                return CustomError("Invalid tapdance pagination");
            Bytes body;
            unsigned count = 0;
            bool more = false;
            uint32_t last = 0;
            for (const auto& [id, e] : model.entries) {
                if (args.Value(2) && id <= args.Value(1))
                    continue;
                if (count == 4) {
                    more = true;
                    break;
                }
                Bytes row;
                Number(row, 1, id);
                Text(row, 2, e.name);
                Number(row, 3, Dirty(id));
                Message(body, 1, row);
                last = id;
                ++count;
            }
            Number(body, 2, more);
            Number(body, 3, last);
            Bytes out;
            Message(out, 3, body);
            return out;
        }
        if (!unlocked)
            return CustomError("Studio is locked");
        if (op->tag == 11) {
            const auto count = Changed(model, saved_);
            model = saved_;
            return Status(count);
        }
        const auto pending = Changed(model, saved_);
        auto next = model, saved = saved_;
        uint32_t id = uint32_t(args.Value(1));
        bool persist = false, write = false, state = false, global = false;
        if (op->tag == 6) {
            const auto* name = args.Get(1);
            if (!name || name->wire != 2 || !args.Numbers({2}) || args.Value(2) > 1)
                return CustomError("Invalid tapdance name/persist");
            if (next.next_id > 65535)
                return CustomError("Tapdance ID space exhausted");
            DanceEntry e;
            e.name.assign(reinterpret_cast<const char*>(name->data), name->size);
            id = next.next_id++;
            next.entries[id] = e;
            saved.next_id = next.next_id;
            if (args.Value(2))
                saved.entries[id] = e;
            write = state = true;
        } else if (op->tag == 10) {
            saved = next;
            write = true;
        } else if (op->tag == 4) {
            if (!args.Numbers({1, 2}) || !args.Value(1) || args.Value(1) > 2000 ||
                args.Value(2) > 1)
                return CustomError("Invalid tapdance interval");
            next.interval = args.Value(1);
            if (args.Value(2)) {
                saved.interval = next.interval;
                write = true;
            }
            global = true;
        } else {
            if (!args.Numbers({1}) || args.Value(1) > 65535)
                return CustomError("Invalid tapdance ID");
            auto at = next.entries.find(id);
            if (at == next.entries.end())
                return CustomError("Unknown tapdance ID");
            if (op->tag == 2) {
                const auto* binding = args.Get(3);
                const auto count = args.Value(2);
                if (!args.Numbers({2, 4}) || count < 1 || count > 3 || args.Value(4) > 1 ||
                    !binding || binding->wire != 2 ||
                    !DanceModel::Action(Reader(binding->data, binding->size),
                                        at->second.actions[count - 1]))
                    return CustomError("Invalid or recursive tapdance binding");
                persist = args.Value(4);
                state = true;
            } else if (op->tag == 7) {
                const auto* name = args.Get(2);
                if (!args.Numbers({3}) || args.Value(3) > 1 || !name || name->wire != 2)
                    return CustomError("Invalid tapdance rename");
                at->second.name.assign(reinterpret_cast<const char*>(name->data), name->size);
                persist = args.Value(3);
                state = true;
            } else if (op->tag == 3 || op->tag == 8) {
                if (!args.Numbers({2}) || args.Value(2) > 1)
                    return CustomError("Invalid tapdance delete");
                next.entries.erase(id);
                persist = op->tag == 8 || args.Value(2);
            } else
                return CustomError("Unsupported tapdance operation");
            if (persist) {
                write = true;
                auto e = next.entries.find(id);
                if (e == next.entries.end())
                    saved.entries.erase(id);
                else
                    saved.entries[id] = e->second;
            }
        }
        if (!next.Valid() || !saved.Valid())
            return CustomError("Tapdance constraint or pool limit exceeded");
        if (write && !save(saved.Export()))
            return CustomError("Tapdance flash commit failed");
        model = std::move(next);
        if (write)
            saved_ = std::move(saved);
        return state ? State(id) : global ? Global() : Status(op->tag == 10 ? pending : 1);
    }

private:
    DanceModel saved_;
    static unsigned Changed(const DanceModel& a, const DanceModel& b) {
        unsigned count = a.interval != b.interval;
        for (const auto& item : a.entries) {
            const auto at = b.entries.find(item.first);
            count += at == b.entries.end() || !(item.second == at->second);
        }
        for (const auto& item : b.entries)
            count += !a.entries.count(item.first);
        return count;
    }
    bool Dirty(uint16_t id) const {
        auto a = model.entries.find(id), b = saved_.entries.find(id);
        return a == model.entries.end() || b == saved_.entries.end() || !(a->second == b->second);
    }
    Bytes State(uint16_t id) const {
        auto e = model.entries.find(id);
        if (e == model.entries.end())
            return CustomError("Unknown tapdance ID");
        Bytes config, body, out;
        const unsigned fields[] = {2, 4, 5};
        for (unsigned i = 0; i < 3; i++)
            Message(config, fields[i], e->second.actions[i].Encode());
        Number(body, 1, id);
        Message(body, 2, config);
        Number(body, 3, model.interval);
        Number(body, 4, e->second.Count());
        Text(body, 5, e->second.name);
        Number(body, 6, Dirty(id));
        Message(out, 2, body);
        return out;
    }
    Bytes Global() const {
        Bytes body, out;
        Number(body, 1, model.interval);
        Number(body, 2, 16);
        Number(body, 3, model.entries.size());
        Number(body, 4, 24);
        Number(body, 5, 1024);
        Number(body, 6, model.Used());
        Number(body, 7, model.Export() != saved_.Export());
        Number(body, 8, 2);
        Message(out, 4, body);
        return out;
    }
    static Bytes Status(unsigned count) {
        Bytes body, out;
        Number(body, 1, count);
        Message(out, 5, body);
        return out;
    }
};
}  // namespace cube_dongle::studio

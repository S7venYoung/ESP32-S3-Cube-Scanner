#pragma once
#include <map>
#include "studio_diagnostics.h"
namespace cube_dongle::studio {
class CustomSettings {
public:
    struct Entry {
        Bytes scalar;
        std::vector<Bytes> array;
        bool is_array = false;
    };
    using Entries = std::map<std::string, Entry>;
    using Notify = std::function<void(const Bytes&)>;
    CustomSettings() {
        live_ = Defaults();
        saved_ = live_;
    }
    unsigned Term() const { return uint32_t(Reader(live_.at("hold_tap.term_ms").scalar).Value(2)); }
    void Session(unsigned transport, uint32_t epoch) {
        if (transport != transport_ || epoch != epoch_) {
            transfer_ = {};
            transport_ = transport;
            epoch_ = epoch;
        }
    }
    Bytes Export() const { return Encode(saved_); }
    bool Load(const Bytes& data) {
        Entries next;
        if (!Decode(data, next))
            return false;
        live_ = saved_ = std::move(next);
        return true;
    }
    Bytes Handle(const Bytes& payload, bool unlocked, unsigned subsystem, const Rpc::Save& save,
                 const Notify& notify) {
        Reader request(payload);
        const auto* op = request.Operation();
        if (!request.valid || !op || op->wire != 2)
            return CustomError("Malformed custom settings request");
        Reader args(op->data, op->size);
        if (!args.valid)
            return CustomError("Malformed custom settings arguments");
        Reader reference(nullptr, 0);
        const auto* field = args.Get(1);
        if (field) {
            if (field->wire != 2)
                return CustomError("Invalid setting reference");
            reference = Reader(field->data, field->size);
        }
        if (!reference.valid && field)
            return CustomError("Invalid setting reference");
        const bool scope = op->tag == 1 || (op->tag >= 4 && op->tag <= 6);
        if (!reference.Numbers(scope ? std::initializer_list<uint32_t>{1, 4}
                                     : std::initializer_list<uint32_t>{1, 3, 4}) ||
            (reference.Get(2) && reference.Get(2)->wire != 2) ||
            (scope && reference.Get(3) && reference.Get(3)->wire != 2))
            return CustomError("Malformed setting scope");
        const auto source = reference.Value(scope ? 4 : 3);
        if ((source && source != UINT32_MAX) ||
            (reference.Get(1) && reference.Value(1) != subsystem))
            return CustomError("Custom setting source/subsystem unavailable");
        auto text = [](const Reader& r, unsigned tag) {
            const auto* f = r.Get(tag);
            return f && f->wire == 2 ? std::string(reinterpret_cast<const char*>(f->data), f->size)
                                     : std::string{};
        };
        const auto key = text(reference, 2), prefix = scope ? text(reference, 3) : std::string{};
        auto selected = [&](const std::string& name) {
            return (key.empty() || name == key) &&
                   (prefix.empty() || name.compare(0, prefix.size(), prefix) == 0);
        };
        auto send = [&](const std::string& name, const Entry& entry, unsigned kind, bool meta,
                        bool defaults) {
            const unsigned count = entry.is_array ? std::max<size_t>(1, entry.array.size()) : 1;
            for (unsigned i = 0; i < count; ++i) {
                Bytes event, notification;
                Number(event, 1, kind);
                Message(event, 2, Setting(name, entry, i, subsystem, meta, defaults));
                Message(notification, 1, event);
                notify(notification);
            }
        };
        if (op->tag == 1) {
            unsigned count = 0;
            for (const auto& [name, entry] : live_)
                if (selected(name)) {
                    send(name, entry, 0, args.Value(2), args.Value(3));
                    ++count;
                }
            return Status(count, "Settings listed");
        }
        const auto found = live_.find(key);
        if (op->tag == 2) {
            if (found == live_.end())
                return CustomError("Unknown setting");
            const auto index = reference.Value(4);
            if (index >= std::max<size_t>(1, found->second.array.size()) && found->second.is_array)
                return CustomError("Invalid array index");
            Bytes body, response;
            Message(body, 1,
                    Setting(key, found->second, index, subsystem, args.Value(2), args.Value(3)));
            Message(response, 3, body);
            return response;
        }
        if (!unlocked)
            return CustomError("Studio is locked");
        auto next = live_;
        auto saved = saved_;
        bool persist = false;
        unsigned affected = 0, kind = 1;
        if (op->tag >= 4 && op->tag <= 6) {
            if (op->tag == 4) {
                for (const auto& [name, entry] : live_)
                    if (selected(name)) {
                        saved[name] = entry;
                        ++affected;
                    }
                for (auto i = saved.begin(); i != saved.end();)
                    if (selected(i->first) && !live_.count(i->first))
                        i = saved.erase(i);
                    else
                        ++i;
                persist = true;
                kind = 2;
            } else if (op->tag == 5) {
                for (auto i = next.begin(); i != next.end();)
                    if (selected(i->first)) {
                        i = next.erase(i);
                        ++affected;
                    } else
                        ++i;
                for (const auto& [name, entry] : saved_)
                    if (selected(name))
                        next[name] = entry;
                kind = 3;
            } else {
                const auto defaults = Defaults();
                for (auto i = next.begin(); i != next.end();)
                    if (selected(i->first)) {
                        i = next.erase(i);
                        ++affected;
                    } else
                        ++i;
                for (const auto& [name, entry] : defaults)
                    if (selected(name))
                        next[name] = entry;
                kind = 4;
            }
        } else if (op->tag == 12) {
            if (found == live_.end() || key.compare(0, 10, "cube.user.") != 0)
                return CustomError("Only user settings can be deleted");
            next.erase(key);
            saved.erase(key);
            persist = true;
            affected = 1;
        } else {
            if (key.empty() || key.size() > 64 || key.find('\0') != std::string::npos)
                return CustomError("Invalid setting key");
            if (op->tag == 11) {
                if (found != live_.end() || key.compare(0, 10, "cube.user.") != 0 ||
                    live_.size() >= 10)
                    return CustomError("Keyspace full or invalid creation");
                next[key] = {};
            } else if (found == live_.end())
                return CustomError("Unknown setting");
            auto& entry = next[key];
            if (op->tag == 8) {
                if (!entry.is_array || entry.array.empty())
                    return CustomError("Array is empty or not an array");
                entry.array.pop_back();
                persist = args.Value(2) == 1;
                if (args.Value(2) > 1)
                    return CustomError("Invalid write mode");
            } else if (op->tag == 7) {
                const auto* value = args.Get(2);
                if (!entry.is_array || entry.array.size() >= 16 || !value || value->wire != 2)
                    return CustomError("Invalid array append");
                Bytes scalar(value->data, value->data + value->size);
                if (!Scalar(scalar))
                    return CustomError("Invalid scalar");
                entry.array.push_back(std::move(scalar));
                persist = args.Value(3) == 1;
                if (args.Value(3) > 1)
                    return CustomError("Invalid write mode");
            } else if (op->tag == 10) {
                const auto* chunk = args.Get(4);
                const auto total = args.Value(2), offset = args.Value(3), mode = args.Value(6);
                Reader current(entry.scalar);
                const auto* type = current.Operation();
                if (entry.is_array || !type || (type->tag != 1 && type->tag != 4) || total > 512 ||
                    mode > 1 || !chunk || chunk->wire != 2 || chunk->size > total)
                    return CustomError("Invalid chunked value");
                if (!offset) {
                    transfer_ = {key, Bytes{}, unsigned(total), unsigned(mode)};
                }
                if (transfer_.key != key || transfer_.total != total || transfer_.mode != mode ||
                    offset != transfer_.bytes.size() || chunk->size > total - offset)
                    return CustomError("Out-of-order chunk");
                transfer_.bytes.insert(transfer_.bytes.end(), chunk->data,
                                       chunk->data + chunk->size);
                if (!args.Value(5))
                    return Status(0, "Chunk received; value unchanged");
                if (transfer_.bytes.size() != total)
                    return CustomError("Incomplete value");
                Bytes value;
                Message(value, type->tag, transfer_.bytes);
                transfer_ = {};
                if (!Scalar(value))
                    return CustomError("Invalid chunked scalar");
                entry.scalar = std::move(value);
                persist = mode == 1;
            } else if (op->tag == 3 || op->tag == 11) {
                const auto* value = args.Get(2);
                if (!value || value->wire != 2 || args.Value(3) > 1)
                    return CustomError("Invalid setting value/mode");
                Bytes data(value->data, value->data + value->size);
                Reader decoded(data);
                const auto* type = decoded.Operation();
                if (!decoded.valid || !type)
                    return CustomError("Malformed setting value");
                if (type->tag == 5) {
                    Reader array(type->data, type->size);
                    const auto* scalar = array.Get(3);
                    const auto index = array.Value(1), size = array.Value(2);
                    if (!array.valid || size > 16 || !scalar || scalar->wire != 2 ||
                        !Scalar(Bytes(scalar->data, scalar->data + scalar->size)))
                        return CustomError("Invalid array value");
                    if (op->tag == 11) {
                        if (index || size != 1)
                            return CustomError("Create arrays with one initial element");
                        entry.is_array = true;
                        entry.array = {Bytes(scalar->data, scalar->data + scalar->size)};
                    } else {
                        if (!entry.is_array || index >= entry.array.size() ||
                            size != entry.array.size())
                            return CustomError("Array shape mismatch");
                        entry.array[index] = Bytes(scalar->data, scalar->data + scalar->size);
                    }
                } else {
                    if (entry.is_array || !Scalar(data, true))
                        return CustomError("Invalid scalar setting");
                    entry.scalar = std::move(data);
                }
                persist = args.Value(3) == 1;
            } else
                return CustomError("Unsupported custom setting operation");
            if (!Builtin(key, entry))
                return CustomError("Setting constraint violated");
            affected = 1;
            if (persist)
                saved[key] = entry;
        }
        const auto encoded = Encode(saved);
        if (encoded.size() > 4096 || Encode(next).size() > 4096)
            return CustomError("Custom setting storage limit reached");
        if (persist && !save(encoded))
            return CustomError("Custom settings flash commit failed");
        live_ = std::move(next);
        if (persist)
            saved_ = std::move(saved);
        for (const auto& [name, entry] : live_)
            if (selected(name))
                send(name, entry, kind, true, true);
        return Status(affected, "Settings updated");
    }

private:
    struct Transfer {
        std::string key;
        Bytes bytes;
        unsigned total = 0, mode = 0;
    } transfer_;
    unsigned transport_ = 0;
    uint32_t epoch_ = 0;
    Entries live_, saved_;
    static Bytes Integer(int value) {
        Bytes out;
        Number(out, 2, uint64_t(int64_t(value)));
        return out;
    }
    static Entries Defaults() { return {{"hold_tap.term_ms", {Integer(200), {}, false}}}; }
    static bool Scalar(const Bytes& data, bool behavior_value = false) {
        if (data.size() > 520)
            return false;
        Reader r(data);
        const auto* v = r.Operation();
        if (!r.valid || !v)
            return false;
        if (v->tag == 1 || v->tag == 4)
            return v->wire == 2 && v->size <= 512 &&
                   (v->tag != 4 || !std::memchr(v->data, 0, v->size));
        if (v->tag == 2)
            return v->wire == 0 && int64_t(v->value) >= INT32_MIN && int64_t(v->value) <= INT32_MAX;
        if (v->tag == 3)
            return v->wire == 0 && v->value <= 1;
        if (v->tag == (behavior_value ? 6u : 5u) && v->wire == 2) {
            Reader b(v->data, v->size);
            Binding binding;
            return b.valid && b.Value(1) <= UINT32_MAX / 2 &&
                   BindingFromWire(b.Value(1) * 2, b.Value(2), b.Value(3), binding);
        }
        return false;
    }
    static bool Builtin(const std::string& key, const Entry& entry) {
        if (key.compare(0, 10, "cube.user.") == 0)
            return true;
        if (entry.is_array)
            return false;
        Reader r(entry.scalar);
        const auto* value = r.Operation();
        if (!value || value->tag != 2)
            return false;
        return key == "hold_tap.term_ms" && value->value >= 1 && value->value <= 5000;
    }
    static Bytes Status(unsigned count, const std::string& text) {
        Bytes body, out;
        Number(body, 1, count);
        Text(body, 2, text);
        Message(out, 2, body);
        return out;
    }
    Bytes Setting(const std::string& key, const Entry& entry, unsigned index, unsigned subsystem,
                  bool meta, bool with_default) const {
        Bytes out, value;
        Number(out, 1, subsystem);
        Text(out, 2, key);
        if (meta) {
            Bytes m;
            Number(m, 1, 2);
            Number(m, 2, 0);
            Number(m, 3, 1);
            if (key == "hold_tap.term_ms") {
                Bytes minimum, maximum, range, constraint;
                Number(minimum, 2, 1);
                Number(maximum, 2, 5000);
                Message(range, 1, minimum);
                Message(range, 2, maximum);
                Message(constraint, 1, range);
                Message(m, 4, constraint);
            }
            Message(out, 3, m);
        }
        auto encode_value = [&](const Entry& e, unsigned at) {
            if (!e.is_array)
                return e.scalar;
            Bytes array, wrapped;
            Number(array, 1, at);
            Number(array, 2, e.array.size());
            if (at < e.array.size())
                Message(array, 3, e.array[at]);
            Message(wrapped, 5, array);
            return wrapped;
        };
        const auto saved = saved_.find(key);
        Number(out, 8,
               saved == saved_.end() ||
                   encode_value(saved->second, index) != encode_value(entry, index));
        Message(out, 9, encode_value(entry, index));
        Number(out, 10, 0);
        const auto defaults = Defaults();
        const auto original = defaults.find(key);
        if (with_default && original != defaults.end() && original->second.scalar != entry.scalar)
            Message(out, 11, original->second.scalar);
        return out;
    }
    static Bytes Encode(const Entries& entries) {
        Bytes out{'C', 'S', 'T', 1};
        for (const auto& [name, entry] : entries) {
            Bytes item;
            Text(item, 1, name);
            Number(item, 2, entry.is_array);
            Message(item, 3, entry.scalar);
            for (const auto& value : entry.array)
                Message(item, 4, value);
            Message(out, 1, item);
        }
        const auto hash = Model::Hash(out.data(), out.size());
        for (unsigned i = 0; i < 4; ++i)
            out.push_back(hash >> (8 * i));
        return out;
    }
    static bool Decode(const Bytes& data, Entries& entries) {
        if (data.size() < 8 || data.size() > 4096 || std::memcmp(data.data(), "CST\1", 4))
            return false;
        uint32_t hash = 0;
        for (unsigned i = 0; i < 4; ++i)
            hash |= uint32_t(data[data.size() - 4 + i]) << (8 * i);
        if (hash != Model::Hash(data.data(), data.size() - 4))
            return false;
        Reader body(data.data() + 4, data.size() - 8, {1});
        if (!body.valid)
            return false;
        Entries next;
        for (const auto& field : body.Repeated(1)) {
            Reader item(field.data, field.size, {4});
            const auto* name = item.Get(1);
            const auto* scalar = item.Get(3);
            if (!item.valid || !name || name->wire != 2 || !name->size || name->size > 64 ||
                !scalar || scalar->wire != 2 || item.Value(2) > 1)
                return false;
            const std::string key(reinterpret_cast<const char*>(name->data), name->size);
            if (key.find('\0') != std::string::npos || next.count(key) || next.size() >= 10)
                return false;
            Entry entry;
            entry.is_array = item.Value(2);
            entry.scalar = Bytes(scalar->data, scalar->data + scalar->size);
            for (const auto& value : item.Repeated(4)) {
                Bytes v(value.data, value.data + value.size);
                if (value.wire != 2 || !Scalar(v) || entry.array.size() >= 16)
                    return false;
                entry.array.push_back(std::move(v));
            }
            if ((!entry.is_array && !Scalar(entry.scalar, true)) || !Builtin(key, entry))
                return false;
            next[key] = std::move(entry);
        }
        for (const auto& [key, value] : Defaults())
            if (!next.count(key))
                next[key] = value;
        entries = std::move(next);
        return true;
    }
};
}  // namespace cube_dongle::studio

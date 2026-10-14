#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "velvet_keymap.h"

namespace cube_dongle::studio {
// Independent wire implementation of ZMK Studio messages, pinned to
// cormoran/zmk-studio-messages 89b81d2e587fce807b668dff2a6967a40beef421.
using Bytes = std::vector<uint8_t>;
constexpr size_t kLimit = 4096;
struct Field {
    uint32_t tag = 0;
    uint64_t value = 0;
    const uint8_t* data = nullptr;
    size_t size = 0;
    unsigned wire = 0;
};
class Reader {
public:
    Reader(const uint8_t* data, size_t size) {
        if (!data || size > kLimit)
            return;
        size_t at = 0;
        while (at < size) {
            uint64_t key;
            if (!Varint(data, size, at, key) || !key || key > UINT32_MAX ||
                count_ == fields_.size())
                return;
            auto& f = fields_[count_++];
            f.tag = key >> 3;
            f.wire = key & 7;
            if (!f.tag)
                return;
            for (size_t i = 0; i + 1 < count_; ++i)
                if (fields_[i].tag == f.tag)
                    return;
            if (f.wire == 0) {
                if (!Varint(data, size, at, f.value))
                    return;
            } else if (f.wire == 2) {
                uint64_t length;
                if (!Varint(data, size, at, length) || length > size - at)
                    return;
                f.data = data + at;
                f.size = length;
                at += length;
            } else if (f.wire == 1 || f.wire == 5) {
                size_t skip = f.wire == 1 ? 8 : 4;
                if (skip > size - at)
                    return;
                at += skip;
            } else
                return;
        }
        valid = true;
    }
    explicit Reader(const Bytes& data) : Reader(data.data(), data.size()) {}
    bool valid = false;
    const Field* Get(uint32_t tag) const {
        for (size_t i = 0; i < count_; ++i)
            if (fields_[i].tag == tag)
                return &fields_[i];
        return nullptr;
    }
    uint64_t Value(uint32_t tag) const {
        const auto* f = Get(tag);
        return f && f->wire == 0 ? f->value : 0;
    }
    bool Numbers(std::initializer_list<uint32_t> tags) const {
        for (auto tag : tags) {
            const auto* f = Get(tag);
            if (f && f->wire != 0)
                return false;
        }
        return true;
    }
    const Field* Operation() const { return count_ == 1 ? &fields_[0] : nullptr; }

private:
    static bool Varint(const uint8_t* data, size_t size, size_t& at, uint64_t& value) {
        value = 0;
        for (unsigned i = 0; i < 10 && at < size; ++i) {
            uint8_t b = data[at++];
            if (i == 9 && b > 1)
                return false;
            value |= uint64_t(b & 0x7f) << (7 * i);
            if (!(b & 0x80))
                return true;
        }
        return false;
    }
    std::array<Field, 16> fields_{};
    size_t count_ = 0;
};
inline void Varint(Bytes& out, uint64_t value) {
    do {
        uint8_t b = value & 0x7f;
        value >>= 7;
        out.push_back(b | (value ? 0x80 : 0));
    } while (value);
}
inline void Number(Bytes& out, uint32_t field, uint64_t value) {
    Varint(out, uint64_t(field) << 3);
    Varint(out, value);
}
inline void Data(Bytes& out, uint32_t field, const uint8_t* bytes, size_t size) {
    Varint(out, (uint64_t(field) << 3) | 2);
    Varint(out, size);
    if (size)
        out.insert(out.end(), bytes, bytes + size);
}
inline void Message(Bytes& out, uint32_t field, const Bytes& value) {
    Data(out, field, value.data(), value.size());
}
inline void Text(Bytes& out, uint32_t field, const std::string& value) {
    Data(out, field, reinterpret_cast<const uint8_t*>(value.data()), value.size());
}
inline void Signed(Bytes& out, uint32_t field, int32_t value) {
    Number(out, field, (uint32_t(value) << 1) ^ uint32_t(-(value < 0)));
}
inline Bytes Frame(const Bytes& payload) {
    Bytes out;
    if (payload.size() > kLimit)
        return out;
    out.reserve(payload.size() + 2);
    out.push_back(0xab);
    for (auto b : payload) {
        if (b >= 0xab && b <= 0xad)
            out.push_back(0xac);
        out.push_back(b);
    }
    out.push_back(0xad);
    return out;
}
class Framer {
public:
    void Reset() {
        active_ = escape_ = dropping_ = false;
        payload_.clear();
    }
    bool Active() const { return active_; }
    template <typename Receive>
    bool Feed(uint8_t b, Receive receive) {
        if (!active_) {
            if (b != 0xab)
                return false;
            Reset();
            active_ = true;
            return true;
        }
        if (escape_) {
            escape_ = false;
            if (b < 0xab || b > 0xad)
                dropping_ = true;
            else
                Append(b);
        } else if (b == 0xab) {
            Reset();
            active_ = true;
        } else if (b == 0xac)
            escape_ = true;
        else if (b == 0xad) {
            active_ = false;
            if (!dropping_)
                receive(payload_);
            Reset();
        } else
            Append(b);
        return true;
    }

private:
    void Append(uint8_t b) {
        if (payload_.size() < kLimit && !dropping_)
            payload_.push_back(b);
        else
            dropping_ = true;
    }
    bool active_ = false, escape_ = false, dropping_ = false;
    Bytes payload_;
};

constexpr uint32_t kKp = 1, kMo = 2, kTransparent = 3, kNone = 4, kBt = 5;
constexpr uint32_t kRuntimeMacro = 6;
inline uint32_t Behavior(const Binding& b) {
    switch (b.kind) {
        case Kind::Key:
        case Kind::Media:
            return kKp;
        case Kind::MomentaryLayer:
            return kMo;
        case Kind::Transparent:
            return kTransparent;
        case Kind::HostAction:
            return kBt;
        case Kind::RuntimeMacro:
            return kRuntimeMacro;
        default:
            return kNone;
    }
}
inline uint32_t Param1(const Binding& b) {
    switch (b.kind) {
        case Kind::Key:
            return 0x70000 | b.value | (uint32_t(b.modifiers) << 24);
        case Kind::Media:
            return 0xc0000 | (b.value == 1 ? 0xe9 : b.value == 2 ? 0xea : 0xe2);
        case Kind::MomentaryLayer:
        case Kind::RuntimeMacro:
            return b.value;
        case Kind::HostAction:
            return b.value < 5 ? 3 : b.value == 5 ? 0 : 4;
        default:
            return 0;
    }
}
inline uint32_t Param2(const Binding& b) {
    return b.kind == Kind::HostAction && b.value < 5 ? b.value : 0;
}
inline bool BindingFromWire(uint64_t encoded_id, uint64_t p1, uint64_t p2, Binding& out) {
    if (encoded_id > UINT32_MAX || p1 > UINT32_MAX || p2 > UINT32_MAX || (encoded_id & 1))
        return false;
    const uint64_t id = encoded_id >> 1;
    if (id == kKp && !p2) {
        uint32_t page = (p1 >> 16) & 0xff, usage = p1 & 0xffff, mods = p1 >> 24;
        if (page == 7 && usage >= 4 && usage <= 0xe7) {
            out = {Kind::Key, uint16_t(usage), uint8_t(mods)};
            return true;
        }
        if (page == 12 && !mods && (usage == 0xe9 || usage == 0xea || usage == 0xe2)) {
            out = {Kind::Media, uint16_t(usage == 0xe9 ? 1 : usage == 0xea ? 2 : 4), 0};
            return true;
        }
    } else if (id == kMo && p1 < 3 && !p2) {
        out = {Kind::MomentaryLayer, uint16_t(p1), 0};
        return true;
    } else if (id == kTransparent && !p1 && !p2) {
        out = {Kind::Transparent, 0, 0};
        return true;
    } else if (id == kNone && !p1 && !p2) {
        out = {};
        return true;
    } else if (id == kRuntimeMacro && p1 < 8 && !p2) {
        out = {Kind::RuntimeMacro, uint16_t(p1), 0};
        return true;
    } else if (id == kBt) {
        if (p1 == 3 && p2 < 5) {
            out = {Kind::HostAction, uint16_t(p2), 0};
            return true;
        }
        if (!p2 && (p1 == 0 || p1 == 4)) {
            out = {Kind::HostAction, uint16_t(p1 == 0 ? 5 : 6), 0};
            return true;
        }
    }
    return false;
}
inline Bytes EncodeBinding(const Binding& b) {
    Bytes out;
    Signed(out, 1, Behavior(b));
    Number(out, 2, Param1(b));
    Number(out, 3, Param2(b));
    return out;
}
struct Model {
    KeyboardState<61, 3>::Keymap map = VelvetKeymap();
    std::array<std::string, 3> names{{"BASE", "NVA", "SYS"}};
    Bytes Keymap() const {
        Bytes out;
        for (size_t l = 0; l < 3; ++l) {
            Bytes layer;
            Number(layer, 1, l);
            Text(layer, 2, names[l]);
            for (const auto& b : map[l])
                Message(layer, 3, EncodeBinding(b));
            Message(out, 1, layer);
        }
        Number(out, 2, 0);
        Number(out, 3, 16);
        return out;
    }
    static uint32_t Hash(const uint8_t* data, size_t size) {
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < size; ++i)
            h = (h ^ data[i]) * 16777619u;
        return h;
    }
    Bytes Export() const {
        for (const auto& name : names)
            if (!ValidName(name))
                return {};
        Bytes out{'C', 'V', 'S', 1};
        for (size_t l = 0; l < 3; ++l) {
            std::array<uint8_t, 17> name{};
            std::memcpy(name.data(), names[l].data(), names[l].size());
            out.insert(out.end(), name.begin(), name.end());
            for (const auto& b : map[l]) {
                out.push_back(uint8_t(b.kind));
                out.push_back(b.value & 0xff);
                out.push_back(b.value >> 8);
                out.push_back(b.modifiers);
            }
        }
        uint32_t h = Hash(out.data(), out.size());
        for (unsigned i = 0; i < 4; ++i)
            out.push_back(h >> (8 * i));
        return out;
    }
    bool Load(const Bytes& data) {
        if (data.size() != 4 + 3 * (17 + 61 * 4) + 4 || std::memcmp(data.data(), "CVS\1", 4))
            return false;
        uint32_t expected = 0;
        for (unsigned i = 0; i < 4; ++i)
            expected |= uint32_t(data[data.size() - 4 + i]) << (8 * i);
        if (Hash(data.data(), data.size() - 4) != expected)
            return false;
        Model next;
        size_t at = 4;
        for (size_t l = 0; l < 3; ++l) {
            const auto* name = reinterpret_cast<const char*>(data.data() + at);
            if (name[16] || !ValidName(std::string(name, strnlen(name, 17))))
                return false;
            next.names[l] = name;
            at += 17;
            for (auto& b : next.map[l]) {
                const unsigned kind = data[at++];
                const uint16_t low = data[at++];
                const uint16_t value = low | (uint16_t(data[at++]) << 8);
                const uint8_t mods = data[at++];
                if (kind > unsigned(Kind::RuntimeMacro))
                    return false;
                b = {Kind(kind), value, mods};
                Binding check;
                if (!BindingFromWire(Behavior(b) * 2, Param1(b), Param2(b), check) ||
                    b.kind != check.kind || b.value != check.value ||
                    b.modifiers != check.modifiers)
                    return false;
            }
        }
        *this = std::move(next);
        return true;
    }
    static bool ValidName(const std::string& name) {
        if (name.empty() || name.size() > 16)
            return false;
        for (auto c : name)
            if (uint8_t(c) < 32 || uint8_t(c) == 127)
                return false;
        return true;
    }
};

// Position order and geometry from velvet-layouts.dtsi on zmk-esp32-cube.
inline Bytes PhysicalLayouts() {
    constexpr std::array<std::array<int, 3>, 61> coords{
        {{0, 63, 0},      {100, 63, 0},      {200, 25, 0},       {300, 0, 0},    {400, 25, 0},
         {500, 25, 0},    {1250, 25, 0},     {1350, 25, 0},      {1450, 0, 0},   {1550, 25, 0},
         {1650, 63, 0},   {1750, 63, 0},     {0, 163, 0},        {100, 163, 0},  {200, 125, 0},
         {300, 100, 0},   {400, 125, 0},     {500, 125, 0},      {725, 313, 0},  {1250, 125, 0},
         {1350, 125, 0},  {1450, 100, 0},    {1550, 125, 0},     {1650, 163, 0}, {1750, 163, 0},
         {0, 263, 0},     {100, 263, 0},     {200, 225, 0},      {300, 200, 0},  {400, 225, 0},
         {500, 225, 0},   {675, 213, 0},     {1250, 225, 0},     {1350, 225, 0}, {1450, 200, 0},
         {1550, 225, 0},  {1650, 263, 0},    {1750, 263, 0},     {0, 363, 0},    {100, 363, 0},
         {200, 325, 0},   {300, 300, 0},     {400, 325, 0},      {500, 325, 0},  {625, 313, 0},
         {1250, 325, 0},  {1350, 325, 0},    {1450, 300, 0},     {1550, 325, 0}, {1650, 363, 0},
         {1750, 363, 0},  {200, 425, 0},     {300, 400, 0},      {425, 438, 0},  {649, 285, 1500},
         {823, 80, 3000}, {655, 992, -3000}, {1013, 757, -1500}, {1300, 438, 0}, {1450, 400, 0},
         {1550, 425, 0}}};
    Bytes layout;
    Text(layout, 1, "Cube Velvet Pro");
    for (const auto& c : coords) {
        Bytes key;
        Signed(key, 1, 100);
        Signed(key, 2, 100);
        Signed(key, 3, c[0]);
        Signed(key, 4, c[1]);
        Signed(key, 5, c[2]);
        Message(layout, 2, key);
    }
    Bytes out;
    Number(out, 1, 0);
    Message(out, 2, layout);
    return out;
}

class Rpc {
public:
    using Save = std::function<bool(const Bytes&)>;
    struct CustomSubsystem {
        std::string identifier, ui_url;
        std::function<Bytes(const Bytes&, bool)> handle;
    };
    std::vector<CustomSubsystem> custom;
    Model model;
    Rpc() : saved_(model.Export()) {}
    void Load(const Bytes& data) {
        if (model.Load(data))
            saved_ = model.Export();
    }
    bool Unlocked() const { return unlocked_; }
    bool Dirty() const { return model.Export() != saved_; }
    Bytes LockState(bool value) {
        unlocked_ = value;
        Bytes core;
        Number(core, 1, value ? 1 : 0);
        Bytes notification;
        Message(notification, 2, core);
        Bytes out;
        Message(out, 2, notification);
        return out;
    }
    Bytes Handle(const Bytes& payload, const Save& save) {
        Reader request(payload);
        uint32_t id = uint32_t(request.Value(1));
        if (!request.valid || !request.Numbers({1}) || request.Value(1) > UINT32_MAX)
            return Error(id, 3);
        const Field* subsystem = nullptr;
        for (auto tag : {3u, 4u, 5u, 100u})
            if (const auto* f = request.Get(tag)) {
                if (subsystem || f->wire != 2)
                    return Error(id, 3);
                subsystem = f;
            }
        if (!subsystem)
            return Error(id, 2);
        Reader body(subsystem->data, subsystem->size);
        const auto* op = body.Operation();
        if (!body.valid || !op)
            return Error(id, 3);
        Bytes reply;
        if (subsystem->tag == 3) {
            if (op->wire != 0)
                return Error(id, 3);
            if (op->tag == 1) {
                Bytes info;
                Text(info, 1, "Cube Velvet Pro");
                Text(info, 2, serial);
                Message(reply, 1, info);
            } else if (op->tag == 2)
                Number(reply, 2, unlocked_ ? 1 : 0);
            else if (op->tag == 3) {
                unlocked_ = false;
                return NoResponse(id);
            } else if (op->tag == 4) {
                if (!unlocked_)
                    return Error(id, 1);
                Model defaults;
                auto blob = defaults.Export();
                bool ok = save(blob);
                if (ok) {
                    model = std::move(defaults);
                    saved_ = std::move(blob);
                }
                Number(reply, 4, ok);
            } else
                return Error(id, 2);
        } else if (subsystem->tag == 100) {
            if (op->wire != 2)
                return Error(id, 3);
            if (op->tag == 1) {
                Bytes list;
                for (size_t i = 0; i < custom.size(); ++i) {
                    Bytes info;
                    Number(info, 1, i);
                    Text(info, 2, custom[i].identifier);
                    Text(info, 3, custom[i].ui_url);
                    Message(list, 1, info);
                }
                Message(reply, 1, list);
            } else if (op->tag == 2) {
                Reader call(op->data, op->size);
                const auto* data = call.Get(2);
                if (!call.valid || !call.Numbers({1}) || !data || data->wire != 2)
                    return Error(id, 3);
                const auto index = call.Value(1);
                if (index >= custom.size() || !custom[index].handle)
                    return Error(id, 2);
                Bytes input(data->data, data->data + data->size);
                const auto result = custom[index].handle(input, unlocked_);
                if (result.size() > kLimit - 32)
                    return Error(id, 3);
                Bytes response;
                Number(response, 1, index);
                Message(response, 2, result);
                Message(reply, 2, response);
            } else
                return Error(id, 2);
        } else if (subsystem->tag == 4) {
            if (op->tag == 1 && op->wire == 0) {
                Bytes list;
                for (uint32_t b = 1; b <= 6; ++b)
                    Number(list, 1, b);
                Message(reply, 1, list);
            } else if (op->tag == 2 && op->wire == 2) {
                Reader detail(op->data, op->size);
                if (!detail.valid || !detail.Numbers({1}))
                    return Error(id, 3);
                auto b = detail.Value(1);
                if (b < 1 || b > 6)
                    return Error(id, 2);
                Message(reply, 2, Details(b));
            } else
                return Error(id, 2);
        } else {
            if (op->tag == 1 && op->wire == 0)
                Message(reply, 1, model.Keymap());
            else if (op->tag == 3 && op->wire == 0)
                Number(reply, 3, model.Export() != saved_);
            else if (op->tag == 6 && op->wire == 0)
                Message(reply, 6, PhysicalLayouts());
            else {
                if (!unlocked_)
                    return Error(id, 1);
                if (op->tag == 2 && op->wire == 2) {
                    Reader edit(op->data, op->size);
                    if (!edit.valid || !edit.Numbers({1, 2}))
                        return Error(id, 3);
                    uint64_t layer = edit.Value(1), position = edit.Value(2);
                    const auto* bf = edit.Get(3);
                    if (layer >= 3 || position >= 61)
                        Number(reply, 2, 1);
                    else if (!bf || bf->wire != 2)
                        return Error(id, 3);
                    else {
                        Reader binding(bf->data, bf->size);
                        if (!binding.valid || !binding.Numbers({1, 2, 3}))
                            return Error(id, 3);
                        Binding value;
                        if (!BindingFromWire(binding.Value(1), binding.Value(2), binding.Value(3),
                                             value))
                            Number(reply, 2, 3);
                        else {
                            model.map[layer][position] = value;
                            Number(reply, 2, 0);
                        }
                    }
                } else if (op->tag == 4 && op->wire == 0) {
                    Bytes result;
                    auto blob = model.Export();
                    if (save(blob)) {
                        saved_ = std::move(blob);
                        Number(result, 1, 1);
                    } else
                        Number(result, 2, 1);
                    Message(reply, 4, result);
                } else if (op->tag == 5 && op->wire == 0) {
                    bool ok = model.Load(saved_);
                    Number(reply, 5, ok);
                } else if (op->tag == 7 && op->wire == 0) {
                    Bytes result;
                    if (op->value == 0)
                        Message(result, 1, model.Keymap());
                    else
                        Number(result, 2, 2);
                    Message(reply, 7, result);
                } else if (op->tag == 12 && op->wire == 2) {
                    Reader props(op->data, op->size);
                    if (!props.valid || !props.Numbers({1}))
                        return Error(id, 3);
                    const auto* name = props.Get(2);
                    uint64_t layer = props.Value(1);
                    if (layer >= 3)
                        Number(reply, 12, 2);
                    else if (!name || name->wire != 2 || name->size > 16)
                        Number(reply, 12, 1);
                    else {
                        std::string value(reinterpret_cast<const char*>(name->data), name->size);
                        if (!Model::ValidName(value))
                            Number(reply, 12, 1);
                        else {
                            model.names[layer] = value;
                            Number(reply, 12, 0);
                        }
                    }
                } else
                    return Error(id, 2);
            }
        }
        return Response(id, subsystem->tag, reply);
    }
    std::string serial = "cube";

private:
    static Bytes Response(uint32_t id, uint32_t subsystem, const Bytes& payload) {
        Bytes body;
        Number(body, 1, id);
        Message(body, subsystem, payload);
        Bytes out;
        Message(out, 1, body);
        return out;
    }
    static Bytes Error(uint32_t id, unsigned error) {
        Bytes meta;
        Number(meta, 2, error);
        return Response(id, 2, meta);
    }
    static Bytes NoResponse(uint32_t id) {
        Bytes meta;
        Number(meta, 1, 1);
        return Response(id, 2, meta);
    }
    static Bytes Details(uint32_t id) {
        Bytes out;
        Number(out, 1, id);
        const char* names[] = {"",     "Key Press", "Momentary Layer", "Transparent",
                               "None", "Bluetooth", "Runtime Macro"};
        Text(out, 2, names[id]);
        if (id == kKp) {
            Bytes set;
            Bytes usage, hid;
            Number(hid, 1, 0xe7);
            Number(hid, 2, 0);
            Message(usage, 5, hid);
            Message(set, 1, usage);
            for (auto v : {0xe9u, 0xeau, 0xe2u}) {
                Bytes constant;
                Number(constant, 3, 0xc0000 | v);
                Text(constant, 1, v == 0xe9 ? "Volume Up" : v == 0xea ? "Volume Down" : "Mute");
                Message(set, 1, constant);
            }
            Message(out, 3, set);
        } else if (id == kRuntimeMacro) {
            Bytes set, slot, range;
            Number(range, 1, 0);
            Number(range, 2, 7);
            Message(slot, 4, range);
            Message(set, 1, slot);
            Message(out, 3, set);
        } else if (id == kMo) {
            Bytes set, layer;
            Message(layer, 6, {});
            Message(set, 1, layer);
            Message(out, 3, set);
        } else if (id == kBt) {
            for (auto command : {3u, 0u, 4u}) {
                Bytes set, p1, p2;
                Number(p1, 3, command);
                Text(p1, 1, command == 3 ? "BT_SEL" : command == 0 ? "BT_CLR" : "BT_CLR_ALL");
                Message(set, 1, p1);
                if (command == 3) {
                    Bytes range;
                    Number(range, 1, 0);
                    Number(range, 2, 4);
                    Message(p2, 4, range);
                } else
                    Message(p2, 2, {});
                Message(set, 2, p2);
                Message(out, 3, set);
            }
        }
        return out;
    }
    bool unlocked_ = false;
    Bytes saved_;
};
}  // namespace cube_dongle::studio

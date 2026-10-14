#pragma once
#include "studio_diagnostics.h"
namespace cube_dongle::studio {
struct DefaultLayers {
    std::array<int8_t, 6> endpoints{{-1, -1, -1, -1, -1, -1}}, os_layers{{-1, -1, -1, -1, -1, -1}};
    std::array<uint8_t, 5> overrides{};  // Protocol OS enum: 0 auto, 2..6 explicit.
    std::array<uint8_t, 6> detected{};   // Runtime observations, never persisted as certainty.
    Bytes Export() const {
        Bytes out{'C', 'D', 'L', 1};
        for (auto value : endpoints)
            out.push_back(uint8_t(value));
        for (auto value : os_layers)
            out.push_back(uint8_t(value));
        out.insert(out.end(), overrides.begin(), overrides.end());
        const auto hash = Model::Hash(out.data(), out.size());
        for (unsigned i = 0; i < 4; ++i)
            out.push_back(hash >> (8 * i));
        return out;
    }
    bool Load(const Bytes& data) {
        if (data.size() != 25 || std::memcmp(data.data(), "CDL\1", 4))
            return false;
        uint32_t hash = 0;
        for (unsigned i = 0; i < 4; ++i)
            hash |= uint32_t(data[21 + i]) << (8 * i);
        if (hash != Model::Hash(data.data(), 21))
            return false;
        DefaultLayers next;
        for (size_t i = 0; i < 6; ++i) {
            next.endpoints[i] = int8_t(data[4 + i]);
            next.os_layers[i] = int8_t(data[10 + i]);
            if (next.endpoints[i] < -2 || next.endpoints[i] > 3 || next.os_layers[i] < -1 ||
                next.os_layers[i] > 3)
                return false;
        }
        for (size_t i = 0; i < 5; ++i) {
            next.overrides[i] = data[16 + i];
            if (next.overrides[i] > 6)
                return false;
            if (next.overrides[i] == 1)
                next.overrides[i] = 0;
        }
        *this = next;
        return true;
    }
    unsigned EffectiveOs(unsigned endpoint) const {
        return endpoint && endpoint <= 5 && overrides[endpoint - 1] >= 2
                   ? overrides[endpoint - 1] - 1
               : endpoint < 6 ? detected[endpoint]
                              : 0;
    }
    int Resolve(unsigned endpoint, const Model& model) const {
        if (endpoint >= 6)
            return -1;
        const int layer =
            endpoints[endpoint] == -2 ? os_layers[EffectiveOs(endpoint)] : endpoints[endpoint];
        return layer >= 0 && model.Active(layer) ? layer : -1;
    }
    Bytes Handle(const Bytes& payload, bool unlocked, const Model& model, unsigned endpoint,
                 const Rpc::Save& save) {
        Reader request(payload);
        const auto* op = request.Operation();
        if (!request.valid || !op || op->wire != 2)
            return CustomError("Invalid default layer request");
        Reader args(op->data, op->size);
        if (!args.valid || !args.Numbers({1, 2}))
            return CustomError("Invalid default layer arguments");
        if (op->tag != 1) {
            if (!unlocked)
                return CustomError("Studio is locked");
            const auto index = args.Value(1);
            const int64_t value = int64_t(args.Value(2));
            if ((op->tag != 2 && op->tag != 3) || index >= 6 || value < (op->tag == 2 ? -2 : -1) ||
                value > 3 || (value >= 0 && !model.Active(value)))
                return CustomError("Invalid default layer mapping");
            auto next = *this;
            (op->tag == 2 ? next.endpoints : next.os_layers)[index] = value;
            if (!save(next.Export()))
                return CustomError("Default layer flash commit failed");
            *this = next;
        }
        Bytes body, response;
        for (unsigned i = 0; i < 6; ++i) {
            Bytes item;
            Number(item, 1, i);
            Number(item, 2, i == 0);
            Number(item, 3, i ? i - 1 : 0);
            Number(item, 4, uint64_t(int64_t(endpoints[i])));
            Message(body, 1, item);
            item.clear();
            Number(item, 1, i);
            Number(item, 2, uint64_t(int64_t(os_layers[i])));
            Message(body, 2, item);
        }
        Number(body, 3, endpoint);
        Number(body, 4, EffectiveOs(endpoint));
        Number(body, 5, uint64_t(int64_t(Resolve(endpoint, model))));
        Number(body, 6, model.count);
        Number(body, 7, 1);
        Message(response, 2, body);
        return response;
    }
    Bytes Os(const Bytes& payload, bool unlocked, unsigned endpoint, uint8_t bonded, bool usb,
             bool ble, const Rpc::Save& save) {
        Reader request(payload);
        const auto* op = request.Operation();
        if (!request.valid || !op || op->wire != 2)
            return CustomError("Invalid OS request");
        Reader args(op->data, op->size);
        if (!args.valid || !args.Numbers({1, 2}))
            return CustomError("Invalid OS arguments");
        if (op->tag == 2) {
            if (!unlocked)
                return CustomError("Studio is locked");
            if (args.Value(1) >= 5 || args.Value(2) > 6)
                return CustomError("Invalid BLE OS override");
            auto next = *this;
            next.overrides[args.Value(1)] = args.Value(2) <= 1 ? 0 : args.Value(2);
            if (!save(next.Export()))
                return CustomError("OS override flash commit failed");
            *this = next;
        } else if (op->tag != 1)
            return CustomError("Unknown OS operation");
        auto profile = [&](unsigned i) {
            Bytes p;
            Number(p, 1, i);
            Number(p, 2, bool(bonded & (1u << i)));
            Number(p, 3, ble && endpoint == i + 1);
            Number(p, 4, detected[i + 1] + 1);
            Number(p, 5, overrides[i]);
            Number(p, 6, EffectiveOs(i + 1) + 1);
            return p;
        };
        Bytes body, response;
        if (op->tag == 2) {
            Message(body, 1, profile(args.Value(1)));
            Message(response, 3, body);
        } else {
            Bytes u;
            Number(u, 1, usb);
            Number(u, 2, detected[0] + 1);
            Message(body, 1, u);
            for (unsigned i = 0; i < 5; ++i)
                Message(body, 2, profile(i));
            Number(body, 3, endpoint ? endpoint - 1 : 0);
            Number(body, 4, EffectiveOs(endpoint) + 1);
            Message(response, 2, body);
        }
        return response;
    }
};
}  // namespace cube_dongle::studio

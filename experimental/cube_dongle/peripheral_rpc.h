#pragma once
#include "studio_diagnostics.h"

namespace cube_dongle::studio {
// Native relay carrier sizes are ABI facts of the pinned nRF/Zephyr modules.
// Watchdog nanopb 0.4.9.1: RelayRequest_size=114, RelayResponse_size=87;
// ARM EABI aligns uint16_t to 2 bytes (source,pad,size,payload,pad).
inline Bytes WatchdogCarrier(uint32_t id, const Bytes& request) {
    Bytes inner;
    Number(inner, 1, id);
    Message(inner, 2, request);
    if (inner.size() > 114)
        return {};
    Bytes carrier(118, 0);
    carrier[0] = 255;
    carrier[2] = inner.size();
    std::copy(inner.begin(), inner.end(), carrier.begin() + 4);
    return carrier;
}
inline Bytes DiagnosticsCarrier(uint8_t id, const Bytes& request) {
    if (request.empty() || request.size() > 32)
        return {};
    Bytes carrier(35, 0);
    carrier[0] = 255;
    carrier[1] = id;
    carrier[2] = request.size();
    std::copy(request.begin(), request.end(), carrier.begin() + 3);
    return carrier;
}
struct PeripheralPending {
    unsigned source = 0, transport = 0;
    uint32_t id = 0, epoch = 0, deadline = 0;
    std::string subsystem;
    Bytes incidents;
    uint32_t total = 0, offset = 0;
    unsigned count = 0;
    bool Expired(uint32_t now) const { return int32_t(now - deadline) >= 0; }
};
inline bool WatchdogReply(const Bytes& carrier, PeripheralPending& pending, Bytes& response) {
    if (carrier.size() != 92)
        return false;
    const size_t size = carrier[2] | (size_t(carrier[3]) << 8);
    if (!size || size > 87)
        return false;
    Reader relay(carrier.data() + 4, size);
    if (!relay.valid || relay.Value(1) != pending.id)
        return false;
    if (const auto* field = relay.Get(2)) {
        if (field->wire != 2)
            return false;
        Reader body(field->data, field->size);
        const auto* op = body.Operation();
        if (!body.valid || !op || op->wire != 2 || (op->tag != 1 && op->tag != 2 && op->tag != 4))
            return false;
        response.assign(field->data, field->data + field->size);
        return true;
    }
    const auto* field = relay.Get(3);
    if (!field || field->wire != 2)
        return false;
    Reader chunk(field->data, field->size);
    if (!chunk.valid || !chunk.Numbers({1, 2, 3}) || chunk.Value(1) > 1 || chunk.Value(2) > 16 ||
        chunk.Value(3) > chunk.Value(2))
        return false;
    if (pending.count && (pending.total != chunk.Value(2) || pending.offset != chunk.Value(3)))
        return false;
    pending.total = chunk.Value(2);
    pending.offset = chunk.Value(3);
    if (const auto* incident = chunk.Get(4)) {
        if (chunk.Value(1) || incident->wire != 2 || pending.count >= 4)
            return false;
        Reader record(incident->data, incident->size);
        if (!record.valid)
            return false;
        Bytes stamped;
        for (unsigned tag = 1; tag <= 8; ++tag) {
            const auto* entry = record.Get(tag);
            if (!entry || tag == 2)
                continue;
            if (entry->wire == 0)
                Number(stamped, tag, entry->value);
            else if (entry->wire == 2)
                Message(stamped, tag, Bytes(entry->data, entry->data + entry->size));
            else
                return false;
        }
        Number(stamped, 2, pending.source);
        Message(pending.incidents, 1, stamped);
        ++pending.count;
    }
    if (!chunk.Value(1))
        return false;
    Number(pending.incidents, 2, pending.total);
    Number(pending.incidents, 3, pending.offset);
    Message(response, 3, pending.incidents);
    return true;
}
}  // namespace cube_dongle::studio

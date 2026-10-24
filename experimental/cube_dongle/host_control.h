#pragma once
#include <string>
#include "report_queue.h"

namespace cube_dongle {
enum class HostCommand : uint8_t {
    None,
    Select,
    Clear,
    ClearAll,
    Pair,
    Auto,
    Usb,
    Ble,
    Enable,
    Disable,
    Next,
    ClearSlot,
    Repair
};
struct HostRequest {
    HostCommand command = HostCommand::None;
    uint8_t profile = 0;
};
inline bool ParseHostCommand(const std::string& line, HostRequest& out) {
    HostRequest request;
    if (line.size() == 11 && line.compare(0, 10, "DONGLE BT ") == 0 && line[10] >= '1' &&
        line[10] <= '5')
        request = {HostCommand::Select, static_cast<uint8_t>(line[10] - '1')};
    else if (line == "DONGLE BT NEXT")
        request.command = HostCommand::Next;
    else if (line.size() == 17 && line.compare(0, 16, "DONGLE BT CLEAR ") == 0 && line[16] >= '1' &&
             line[16] <= '5')
        request = {HostCommand::ClearSlot, static_cast<uint8_t>(line[16] - '1')};
    else if (line == "DONGLE BT CLEAR")
        request.command = HostCommand::Clear;
    else if (line == "DONGLE BT CLEAR ALL")
        request.command = HostCommand::ClearAll;
    else if (line == "DONGLE BT PAIR")
        request.command = HostCommand::Pair;
    else if (line == "DONGLE BT REPAIR")
        request.command = HostCommand::Repair;
    else if (line == "DONGLE OUTPUT AUTO")
        request.command = HostCommand::Auto;
    else if (line == "DONGLE OUTPUT USB")
        request.command = HostCommand::Usb;
    else if (line == "DONGLE OUTPUT BLE")
        request.command = HostCommand::Ble;
    else if (line == "DONGLE ENABLE")
        request.command = HostCommand::Enable;
    else if (line == "DONGLE DISABLE")
        request.command = HostCommand::Disable;
    else
        return false;
    out = request;
    return true;
}
// Every endpoint/profile transition requires both old release completion and
// physical neutral input. Never replay keys held during host selection.
class HostBarrier {
public:
    void Begin() { waiting_ = true; }
    bool Accept(const ReportFrame& input, bool released) {
        if (waiting_) {
            if (!released || !input.Neutral())
                return false;
            waiting_ = false;
        }
        return true;
    }
    bool Waiting() const { return waiting_; }

private:
    bool waiting_ = true;
};
}  // namespace cube_dongle

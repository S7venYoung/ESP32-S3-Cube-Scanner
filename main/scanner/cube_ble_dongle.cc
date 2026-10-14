#include "cube_ble_dongle.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_store.h>
#include <nimble/nimble_port.h>
#include <nvs.h>
#include <os/os_mbuf.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "../../experimental/cube_dongle/host_control.h"
#include "../../experimental/cube_dongle/peer_binding.h"
#include "cube_split_bench.h"
#include "cube_usb_dongle.h"

namespace {
using cube_dongle::HostCommand;
using cube_dongle::HostRequest;
using cube_dongle::ReportFrame;
constexpr char kTag[] = "CubeBleDongle";
struct Profile {
    bool bound = false;
    ble_addr_t identity{};
};
Profile profiles[5];
unsigned selected = 0;
enum class Output { Auto, Usb, Ble };
Output preference = Output::Auto;
bool routed_ble = false, enabled = true, configured = false, synced = false;
bool authorized = false, suspended = false, keyboard_sub = false, media_sub = false,
     boot_sub = false;
uint8_t own_type = 0, protocol = 1, leds = 0;
uint16_t connection = BLE_HS_CONN_HANDLE_NONE;
uint16_t keyboard_handle, media_handle, boot_handle;
int64_t pairing_until = 0, security_deadline = 0, inflight_since = 0;
uint32_t revision = 0, flight_revision = 0;
unsigned phase = 0;
uint16_t flight_handle = 0;
bool inflight = false, neutral_sent = false;
cube_dongle::ReportQueue<64> frames;
ReportFrame physical_input;
bool physical_neutral = true;
cube_dongle::HostBarrier barrier;
std::atomic<uint32_t> mailbox{0}, public_status{0};
std::atomic<uint32_t> next_ticket{0}, done_ticket{0};
std::atomic<int> command_result{0};
uint32_t pending_ticket = 0;
HostRequest pending;
ble_npl_callout output_timer;
bool timer_initialized = false;
bool transition = false, old_ble = false;
std::atomic<int> last_result{0};  // zero success, positive NimBLE, negative NVS/configuration

void ReleaseBle() {
    frames.ReleaseAndInhibit();
    ++revision;
    neutral_sent = false;
}
bool Encrypted() {
    ble_gap_conn_desc desc{};
    return connection != BLE_HS_CONN_HANDLE_NONE && ble_gap_conn_find(connection, &desc) == 0 &&
           desc.sec_state.encrypted;
}
bool NotifyReady() {
    return authorized && Encrypted() && !suspended && (protocol ? keyboard_sub : boot_sub);
}
void Publish() {
    public_status = (enabled ? 1u : 0u) | (routed_ble ? 2u : 0u) | (NotifyReady() ? 4u : 0u) |
                    (selected << 8) | (static_cast<unsigned>(preference) << 16) |
                    ((transition || mailbox.load()) ? 1u << 24 : 0u) | (configured ? 1u << 25 : 0u);
}
bool SaveSelection(unsigned profile, Output output) {
    nvs_handle_t handle;
    if (nvs_open("cube_hosts", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    auto rc = nvs_set_u8(handle, "selected", profile);
    if (rc == ESP_OK)
        rc = nvs_set_u8(handle, "output", static_cast<uint8_t>(output));
    if (rc == ESP_OK)
        rc = nvs_commit(handle);
    nvs_close(handle);
    return rc == ESP_OK;
}
bool SaveProfile(unsigned slot, const ble_addr_t* identity) {
    nvs_handle_t handle;
    if (nvs_open("cube_hosts", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    char key[4];
    std::snprintf(key, sizeof(key), "p%u", slot);
    uint8_t blob[7];
    if (identity) {
        blob[0] = identity->type & 1;
        std::memcpy(blob + 1, identity->val, 6);
    }
    auto rc = identity ? nvs_set_blob(handle, key, blob, sizeof(blob)) : nvs_erase_key(handle, key);
    if (rc == ESP_ERR_NVS_NOT_FOUND)
        rc = ESP_OK;
    if (rc == ESP_OK)
        rc = nvs_commit(handle);
    nvs_close(handle);
    return rc == ESP_OK;
}
bool Same(const ble_addr_t& a, const ble_addr_t& b) {
    return (a.type & 1) == (b.type & 1) && !std::memcmp(a.val, b.val, 6);
}
bool ValidIdentity(const ble_addr_t& address) {
    char text[18];
    std::snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", address.val[5],
                  address.val[4], address.val[3], address.val[2], address.val[1], address.val[0]);
    cube_dongle::PeerBinding parsed;
    return cube_dongle::ParseIdentity(text, address.type & 1, parsed);
}
int Gap(ble_gap_event*, void*);
void Advertise() {
    if (!synced || !configured || connection != BLE_HS_CONN_HANDLE_NONE || transition ||
        (!profiles[selected].bound && esp_timer_get_time() >= pairing_until))
        return;
    if (ble_gap_adv_active())
        return;
    static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);
    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance = 0x03c1;
    fields.appearance_is_present = 1;
    fields.uuids16 = &hid_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    char name[24];
    std::snprintf(name, sizeof(name), "Cube ZMK BLE %u", selected + 1);
    ble_svc_gap_device_name_set(name);
    ble_hs_adv_fields response{};
    response.name = reinterpret_cast<const uint8_t*>(name);
    response.name_len = std::strlen(name);
    response.name_is_complete = 1;
    if (!rc)
        rc = ble_gap_adv_rsp_set_fields(&response);
    ble_gap_adv_params params{};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    if (!rc)
        rc = ble_gap_adv_start(own_type, nullptr, BLE_HS_FOREVER, &params, Gap, nullptr);
    if (rc)
        last_result = rc;
}
void Pump() {
    if (!NotifyReady() || inflight)
        return;
    const auto* front = frames.Front();
    if (!front)
        return;
    if (phase == 1 && (!protocol || !media_sub)) {
        if (front->Neutral())
            neutral_sent = true;
        frames.Pop();
        phase = 0;
        return;
    }
    const uint16_t handle = phase == 0 ? (protocol ? keyboard_handle : boot_handle) : media_handle;
    const void* bytes =
        phase == 0 ? static_cast<const void*>(front->keyboard.data()) : &front->media;
    auto* mbuf = ble_hs_mbuf_from_flat(bytes, phase == 0 ? 8 : 1);
    if (!mbuf)
        return;  // bounded retry on the host timer
    inflight = true;
    flight_handle = handle;
    flight_revision = revision;
    inflight_since = esp_timer_get_time();
    const int rc = ble_gatts_notify_custom(connection, handle, mbuf);  // consumes mbuf on error too
    if (rc) {
        inflight = false;
        last_result = rc;
        ReleaseBle();
    }
}
int Gap(ble_gap_event* event, void*) {
    if (event->type == BLE_GAP_EVENT_CONNECT) {
        if (event->connect.status) {
            Advertise();
            return 0;
        }
        connection = event->connect.conn_handle;
        authorized = false;
        protocol = 1;
        suspended = false;
        keyboard_sub = media_sub = boot_sub = false;
        ReleaseBle();
        ble_gap_conn_desc desc{};
        if (ble_gap_conn_find(connection, &desc) ||
            CubeSplitIsKeyboard(desc.peer_id_addr.val, desc.peer_id_addr.type & 1) ||
            (profiles[selected].bound && !Same(profiles[selected].identity, desc.peer_id_addr)) ||
            (!profiles[selected].bound && esp_timer_get_time() >= pairing_until)) {
            ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        security_deadline = esp_timer_get_time() + 15000000;
        if (ble_gap_security_initiate(connection))
            ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT &&
               connection == event->disconnect.conn.conn_handle) {
        connection = BLE_HS_CONN_HANDLE_NONE;
        authorized = false;
        keyboard_sub = media_sub = boot_sub = false;
        inflight = false;
        phase = 0;
        ReleaseBle();
        if (routed_ble)
            barrier.Begin();
        Advertise();
    } else if (event->type == BLE_GAP_EVENT_ENC_CHANGE &&
               connection == event->enc_change.conn_handle) {
        ble_gap_conn_desc desc{};
        bool valid = !event->enc_change.status && !ble_gap_conn_find(connection, &desc) &&
                     desc.sec_state.encrypted && desc.sec_state.bonded &&
                     ValidIdentity(desc.peer_id_addr) &&
                     !CubeSplitIsKeyboard(desc.peer_id_addr.val, desc.peer_id_addr.type & 1);
        for (unsigned i = 0; i < 5; ++i)
            if (i != selected && profiles[i].bound && Same(profiles[i].identity, desc.peer_id_addr))
                valid = false;
        if (valid && profiles[selected].bound)
            valid = Same(profiles[selected].identity, desc.peer_id_addr);
        else if (valid) {
            valid =
                esp_timer_get_time() < pairing_until && SaveProfile(selected, &desc.peer_id_addr);
            if (valid) {
                profiles[selected].bound = true;
                profiles[selected].identity = desc.peer_id_addr;
                pairing_until = 0;
            } else
                last_result = -1;
        }
        authorized = valid;
        ReleaseBle();
        if (routed_ble)
            barrier.Begin();
        if (!valid)
            ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    } else if (event->type == BLE_GAP_EVENT_SUBSCRIBE &&
               connection == event->subscribe.conn_handle) {
        if (event->subscribe.attr_handle == keyboard_handle)
            keyboard_sub = event->subscribe.cur_notify;
        if (event->subscribe.attr_handle == media_handle)
            media_sub = event->subscribe.cur_notify;
        if (event->subscribe.attr_handle == boot_handle)
            boot_sub = event->subscribe.cur_notify;
        ReleaseBle();
        if (routed_ble)
            barrier.Begin();
    } else if (event->type == BLE_GAP_EVENT_NOTIFY_TX &&
               connection == event->notify_tx.conn_handle && inflight &&
               flight_handle == event->notify_tx.attr_handle && !event->notify_tx.indication) {
        inflight = false;
        if (event->notify_tx.status) {
            ReleaseBle();
            phase = 0;
            last_result = event->notify_tx.status;
        } else if (flight_revision != revision)
            phase = 0;
        else if (++phase == 2) {
            if (frames.Front() && frames.Front()->Neutral())
                neutral_sent = true;
            frames.Pop();
            phase = 0;
        }
    } else if (event->type == BLE_GAP_EVENT_REPEAT_PAIRING) {
        return BLE_GAP_REPEAT_PAIRING_IGNORE;  // clear the selected computer slot explicitly
    }
    Publish();
    return 0;
}
enum Field : uintptr_t {
    Info,
    Map,
    Protocol,
    Control,
    Keyboard,
    Media,
    OutputLed,
    Boot,
    Pnp,
    RefKeyboard,
    RefMedia,
    RefOutput
};
int Access(uint16_t conn, uint16_t, ble_gatt_access_ctxt* ctx, void* arg) {
    const auto field = static_cast<Field>(reinterpret_cast<uintptr_t>(arg));
    const bool write = ctx->op == BLE_GATT_ACCESS_OP_WRITE_CHR;
    if (write) {
        uint8_t value;
        if (conn != connection || !authorized || !Encrypted())
            return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        if (OS_MBUF_PKTLEN(ctx->om) != 1 || os_mbuf_copydata(ctx->om, 0, 1, &value))
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (field == Protocol || field == Control) {
            if (value > 1)
                return BLE_ATT_ERR_UNLIKELY;
            if (field == Protocol)
                protocol = value;
            else
                suspended = value == 0;
            ReleaseBle();
            if (routed_ble)
                barrier.Begin();
        } else if (field == OutputLed)
            leds = value;
        else
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        return 0;
    }
    const uint8_t info[] = {0x11, 0x01, 0, 0x02}, pnp[] = {2, 0x3a, 0x30, 0x04, 0x40, 0, 1};
    const uint8_t ref[] = {static_cast<uint8_t>(field == RefMedia ? 2 : 1),
                           static_cast<uint8_t>(field == RefOutput ? 2 : 1)};
    ReportFrame current = authorized && conn == connection ? frames.Latest() : ReportFrame{};
    const void* bytes = nullptr;
    size_t size = 0;
    switch (field) {
        case Info:
            bytes = info;
            size = sizeof(info);
            break;
        case Pnp:
            bytes = pnp;
            size = sizeof(pnp);
            break;
        case Map:
            bytes = CubeUsbReportMap(size);
            break;
        case Protocol:
            bytes = &protocol;
            size = 1;
            break;
        case Keyboard:
        case Boot:
            bytes = current.keyboard.data();
            size = 8;
            break;
        case Media:
            bytes = &current.media;
            size = 1;
            break;
        case OutputLed:
            bytes = &leds;
            size = 1;
            break;
        case RefKeyboard:
        case RefMedia:
        case RefOutput:
            bytes = ref;
            size = 2;
            break;
        default:
            return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }
    return os_mbuf_append(ctx->om, bytes, size) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}
bool RegisterGatt() {
    static const ble_uuid16_t hid = BLE_UUID16_INIT(0x1812), dis = BLE_UUID16_INIT(0x180a),
                              ref = BLE_UUID16_INIT(0x2908);
    static const ble_uuid16_t uuid[] = {BLE_UUID16_INIT(0x2a4a), BLE_UUID16_INIT(0x2a4b),
                                        BLE_UUID16_INIT(0x2a4e), BLE_UUID16_INIT(0x2a4c),
                                        BLE_UUID16_INIT(0x2a4d), BLE_UUID16_INIT(0x2a4d),
                                        BLE_UUID16_INIT(0x2a4d), BLE_UUID16_INIT(0x2a22),
                                        BLE_UUID16_INIT(0x2a32), BLE_UUID16_INIT(0x2a50)};
    static ble_gatt_dsc_def descriptors[3][2]{};
    for (unsigned i = 0; i < 3; ++i) {
        descriptors[i][0].uuid = &ref.u;
        descriptors[i][0].att_flags = BLE_ATT_F_READ;
        descriptors[i][0].access_cb = Access;
        descriptors[i][0].arg = reinterpret_cast<void*>(RefKeyboard + i);
    }
    static ble_gatt_chr_def chars[10]{}, device_info[2]{};
    const Field fields[] = {Info,  Map,       Protocol, Control,  Keyboard,
                            Media, OutputLed, Boot,     OutputLed};
    const uint16_t read = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC;
    const uint16_t write =
        BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC;
    for (unsigned i = 0; i < 9; ++i) {
        auto& c = chars[i];
        c.uuid = &uuid[i].u;
        c.access_cb = Access;
        c.arg = reinterpret_cast<void*>(fields[i]);
        c.flags = read;
    }
    chars[0].flags = BLE_GATT_CHR_F_READ;
    chars[2].flags = read | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC;
    chars[3].flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC;
    chars[4].flags = chars[5].flags = chars[7].flags = read | BLE_GATT_CHR_F_NOTIFY;
    chars[4].val_handle = &keyboard_handle;
    chars[5].val_handle = &media_handle;
    chars[7].val_handle = &boot_handle;
    chars[4].descriptors = descriptors[0];
    chars[5].descriptors = descriptors[1];
    chars[6].descriptors = descriptors[2];
    chars[6].flags = chars[8].flags = read | write;
    device_info[0].uuid = &uuid[9].u;
    device_info[0].access_cb = Access;
    device_info[0].arg = reinterpret_cast<void*>(Pnp);
    device_info[0].flags = BLE_GATT_CHR_F_READ;
    static ble_gatt_svc_def services[3]{};
    services[0].type = services[1].type = BLE_GATT_SVC_TYPE_PRIMARY;
    services[0].uuid = &hid.u;
    services[0].characteristics = chars;
    services[1].uuid = &dis.u;
    services[1].characteristics = device_info;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Cube ZMK BLE");
    return !ble_gatts_count_cfg(services) && !ble_gatts_add_svcs(services);
}
bool ClearSlot(unsigned slot) {
    if (!profiles[slot].bound)
        return true;
    if (CubeSplitIsKeyboard(profiles[slot].identity.val, profiles[slot].identity.type & 1))
        return false;
    const int rc = ble_store_util_delete_peer(&profiles[slot].identity);
    if (rc && rc != BLE_HS_ENOENT) {
        last_result = rc;
        return false;
    }
    if (!SaveProfile(slot, nullptr)) {
        last_result = -1;
        return false;
    }
    profiles[slot] = {};
    return true;
}
bool WantBle() {
    return preference == Output::Ble || (preference == Output::Auto && !CubeUsbConnected());
}
bool OldReleased() {
    return old_ble ? (!NotifyReady() || neutral_sent)
                   : (!CubeUsbConnected() || CubeUsbNeutralDelivered());
}
void BeginTransition(HostRequest request) {
    pending = request;
    transition = true;
    old_ble = routed_ble;
    barrier.Begin();
    CubeUsbRelease();
    ReleaseBle();
    ble_gap_adv_stop();
}
void ApplyPending() {
    const auto request = pending;
    pending = {};
    if (request.command == HostCommand::Pair && profiles[selected].bound) {
        last_result = -3;
        command_result = -3;
        if (pending_ticket)
            done_ticket = pending_ticket;
        transition = false;
        Advertise();
        return;
    }
    auto next_profile = selected;
    auto next_output = preference;
    if (request.command == HostCommand::Select) {
        next_profile = request.profile;
        next_output = Output::Ble;
    }
    if (request.command == HostCommand::Auto)
        next_output = Output::Auto;
    if (request.command == HostCommand::Usb)
        next_output = Output::Usb;
    if (request.command == HostCommand::Ble)
        next_output = Output::Ble;
    if (request.command == HostCommand::Select || request.command == HostCommand::Clear ||
        request.command == HostCommand::ClearAll) {
        if (connection != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
            return;
        }
    }
    bool okay = SaveSelection(next_profile, next_output);
    if (okay && request.command == HostCommand::Clear)
        okay = ClearSlot(selected);
    if (okay && request.command == HostCommand::ClearAll)
        for (unsigned i = 0; i < 5; ++i)
            if (!ClearSlot(i)) {
                okay = false;
                break;
            }
    if (okay) {
        selected = next_profile;
        preference = next_output;
        if (request.command == HostCommand::Enable)
            enabled = true;
        if (request.command == HostCommand::Disable)
            enabled = false;
        if (request.command == HostCommand::Pair || request.command == HostCommand::Select ||
            request.command == HostCommand::Clear || request.command == HostCommand::ClearAll)
            pairing_until = profiles[selected].bound ? 0 : esp_timer_get_time() + 120000000;
        last_result = 0;
    } else
        last_result = -1;
    routed_ble = WantBle();
    if (pending_ticket) {
        command_result = last_result.load();
        done_ticket = pending_ticket;
    }
    transition = false;
    barrier.Begin();
    Advertise();
}
}  // namespace

bool CubeBleHostInit() {
    nvs_handle_t handle;
    const auto rc = nvs_open("cube_hosts", NVS_READONLY, &handle);
    if (rc != ESP_OK && rc != ESP_ERR_NVS_NOT_FOUND)
        return false;
    bool valid = true;
    if (rc == ESP_OK) {
        uint8_t value = 0;
        auto status = nvs_get_u8(handle, "selected", &value);
        if (status == ESP_OK && value < 5)
            selected = value;
        else if (status != ESP_ERR_NVS_NOT_FOUND)
            valid = false;
        value = 0;
        status = nvs_get_u8(handle, "output", &value);
        if (status == ESP_OK && value <= 2)
            preference = static_cast<Output>(value);
        else if (status != ESP_ERR_NVS_NOT_FOUND)
            valid = false;
        for (unsigned i = 0; i < 5; ++i) {
            char key[4];
            std::snprintf(key, sizeof(key), "p%u", i);
            uint8_t blob[7];
            size_t size = sizeof(blob);
            status = nvs_get_blob(handle, key, blob, &size);
            if (status == ESP_OK && size == 7 && blob[0] <= 1) {
                profiles[i].identity.type = blob[0];
                std::memcpy(profiles[i].identity.val, blob + 1, 6);
                profiles[i].bound = ValidIdentity(profiles[i].identity);
                valid &= profiles[i].bound;
            } else if (status != ESP_ERR_NVS_NOT_FOUND)
                valid = false;
        }
        nvs_close(handle);
    }
    for (unsigned i = 0; i < 5; ++i)
        if (profiles[i].bound) {
            if (CubeSplitIsKeyboard(profiles[i].identity.val, profiles[i].identity.type & 1))
                valid = false;
            for (unsigned j = 0; j < i; ++j)
                if (profiles[j].bound && Same(profiles[i].identity, profiles[j].identity))
                    valid = false;
        }
    configured = valid && RegisterGatt();
    ReleaseBle();
    Publish();
    return configured;
}
namespace {
void OutputTimer(ble_npl_event*) {
    CubeDualTick();
    ble_npl_callout_reset(&output_timer, ble_npl_time_ms_to_ticks32(5));
}
void SubmitBle(const ReportFrame& frame) {
    const bool inhibited = frames.Inhibited();
    if (!frames.Push(frame) && !inhibited) {
        ++revision;
        neutral_sent = false;
        barrier.Begin();
        last_result = -4;
    }
}
}  // namespace
void CubeBleHostSync(uint8_t type) {
    own_type = type;
    synced = true;
    if (!timer_initialized) {
        ble_npl_callout_init(&output_timer, nimble_port_get_dflt_eventq(), OutputTimer, nullptr);
        timer_initialized = true;
    }
    ble_npl_callout_reset(&output_timer, ble_npl_time_ms_to_ticks32(5));
    Advertise();
}
void CubeBleHostReset() {
    synced = false;
    if (timer_initialized)
        ble_npl_callout_stop(&output_timer);
    authorized = false;
    connection = BLE_HS_CONN_HANDLE_NONE;
    inflight = false;
    phase = 0;
    ReleaseBle();
    barrier.Begin();
    Publish();
}
void CubeDualTick() {
    if (!synced || !configured)
        return;
    if (connection != BLE_HS_CONN_HANDLE_NONE &&
        ((!authorized && esp_timer_get_time() >= security_deadline) ||
         (inflight && esp_timer_get_time() - inflight_since > 3000000)))
        ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    if (!transition) {
        const auto request = mailbox.exchange(0);
        if (request) {
            pending_ticket = request >> 16;
            BeginTransition({static_cast<HostCommand>(request & 0xff),
                             static_cast<uint8_t>((request >> 8) & 0xff)});
        } else if (routed_ble != WantBle()) {
            pending_ticket = 0;
            BeginTransition({});
        }
    }
    Pump();
    if (transition && OldReleased() && physical_neutral) {
        // Keep the request until the disconnect callback has completed.
        if ((pending.command == HostCommand::Select || pending.command == HostCommand::Clear ||
             pending.command == HostCommand::ClearAll) &&
            connection != BLE_HS_CONN_HANDLE_NONE)
            ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
        else
            ApplyPending();
    }
    if (!transition && enabled &&
        barrier.Accept(physical_input,
                       physical_neutral &&
                           (routed_ble ? (!NotifyReady() || neutral_sent)
                                       : (!CubeUsbConnected() || CubeUsbNeutralDelivered())))) {
        if (routed_ble) {
            if (NotifyReady())
                SubmitBle(physical_input);
        } else
            CubeUsbSubmit(physical_input);
    }
    if (!profiles[selected].bound && esp_timer_get_time() >= pairing_until && ble_gap_adv_active())
        ble_gap_adv_stop();
    Advertise();
    Pump();
    Publish();
}
void CubeDualSubmit(const ReportFrame& frame, uint16_t actions) {
    physical_input = frame;
    // Actual physical-neutral status is supplied separately, including layer keys.
    if (actions) {
        HostRequest request;
        for (unsigned i = 0; i < 5; ++i)
            if (actions & (1u << i))
                request = {HostCommand::Select, static_cast<uint8_t>(i)};
        if (actions & 32)
            request = {HostCommand::Clear, 0};
        if (actions & 64)
            request = {HostCommand::ClearAll, 0};
        uint32_t expected = 0;
        const uint32_t ticket = (next_ticket.fetch_add(1) % 65535) + 1;
        if (request.command != HostCommand::None &&
            !mailbox.compare_exchange_strong(expected, static_cast<uint32_t>(request.command) |
                                                           (request.profile << 8) | (ticket << 16)))
            last_result = -2;
    }
    CubeDualTick();
}
void CubeDualSetNeutral(bool value) { physical_neutral = value; }
std::string CubeDualCommand(const std::string& line) {
    if (line == "DONGLE HOST")
        return CubeDualStatus();
    if (!(public_status.load() & (1u << 25)))
        return "ERR HOST-NOT-INITIALIZED";
    HostRequest request;
    if (!cube_dongle::ParseHostCommand(line, request))
        return "ERR INVALID-HOST-COMMAND";
    uint32_t expected = 0;
    const uint32_t ticket = (next_ticket.fetch_add(1) % 65535) + 1;
    if (!mailbox.compare_exchange_strong(expected, static_cast<uint32_t>(request.command) |
                                                       (request.profile << 8) | (ticket << 16)))
        return "ERR HOST-BUSY";
    return "CUBE-DUAL-CANDIDATE QUEUED " + std::to_string(ticket);
}
bool CubeDualEnabled() { return public_status.load() & 1; }
bool CubeDualUsesBle() { return public_status.load() & 2; }
bool CubeDualBleReady() { return public_status.load() & 4; }
int CubeDualProfile() { return (public_status.load() >> 8) & 7; }
std::string CubeDualStatus() {
    const auto status = public_status.load();
    return "CUBE-HOST/1 PROFILE=" + std::to_string(((status >> 8) & 7) + 1) +
           " OUTPUT=" + ((status & 2) ? "BLE" : "USB") +
           " BLE-READY=" + ((status & 4) ? "1" : "0") +
           " SWITCHING=" + (((status & (1u << 24)) || mailbox.load()) ? "1" : "0") +
           " RESULT=" + std::to_string(last_result.load()) +
           " DONE=" + std::to_string(done_ticket.load()) +
           " COMMAND-RESULT=" + std::to_string(command_result.load());
}

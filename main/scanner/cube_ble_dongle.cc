#include "cube_ble_dongle.h"
#include "application.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <host/ble_att.h>
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
#include <algorithm>
#include <cstdio>
#include <cstring>
#if CONFIG_CUBE_STUDIO_RPC
#include <mutex>
#include "../../experimental/cube_dongle/studio_diagnostics.h"
#include "../../experimental/cube_dongle/studio_transport.h"
#include "cube_studio.h"
#endif
#include "../../experimental/cube_dongle/host_control.h"
#include "../../experimental/cube_dongle/host_cycle.h"
#include "../../experimental/cube_dongle/peer_binding.h"
#include "cube_split_bench.h"
#include "cube_usb_dongle.h"

namespace {
using cube_dongle::HostCommand;
using cube_dongle::HostRequest;
using cube_dongle::ReportFrame;
constexpr char kTag[] = "CubeBleDongle";
#if CONFIG_CUBE_STUDIO_RPC
const ble_uuid128_t studio_uuid = BLE_UUID128_INIT(0x2a, 0x48, 0xc2, 0xb1, 0xcf, 0xc5, 0x67, 0xc9,
                                                   0x07, 0x61, 0x96, 0x01, 0, 0, 0, 0);
const ble_uuid128_t studio_char_uuid = BLE_UUID128_INIT(0x2a, 0x48, 0xc2, 0xb1, 0xcf, 0xc5, 0x67,
                                                        0xc9, 0x07, 0x61, 0x96, 0x01, 1, 0, 0, 0);
std::mutex studio_mutex;
cube_dongle::studio::BlePipe studio_pipe;
std::atomic<bool> studio_ready{false};
std::atomic<uint32_t> studio_epoch{1};
uint16_t studio_handle = 0;
bool studio_subscribed = false;
cube_dongle::studio::ProgressDeadline studio_progress;
#endif
struct Profile {
    bool bound = false;
    ble_addr_t identity{};
};
Profile profiles[5];
unsigned selected = 0;
enum class Output { Auto, Usb, Ble };
Output preference = Output::Auto;
bool routed_ble = false, enabled = true, configured = false, synced = false;
unsigned pending_system = 0;  // 1: ROM download, 2: reboot. Not factory reset.
bool system_old_ble = false;
int64_t system_started = 0;
bool authorized = false, suspended = false, keyboard_sub = false, media_sub = false,
     boot_sub = false, pointer_sub = false;
bool security_pending = false;
uintptr_t host_epoch = 1;
int64_t advertisement_next = 0, transition_started = 0;
std::atomic<uint32_t> ui_notice{0};
uint32_t notice_sequence = 0;
void Notice(unsigned kind) { ui_notice = (++notice_sequence << 8) | (selected << 3) | kind; }
uint8_t own_type = 0, protocol = 1, leds = 0;
uint16_t connection = BLE_HS_CONN_HANDLE_NONE;
uint16_t keyboard_handle, media_handle, boot_handle, pointer_handle;
int64_t pairing_until = 0, security_deadline = 0, inflight_since = 0;
uint32_t revision = 0, flight_revision = 0;
unsigned phase = 0;
uint16_t flight_handle = 0;
bool inflight = false, neutral_sent = false;
cube_dongle::ReportQueue<64> frames;
ReportFrame physical_input;
bool physical_neutral = true, keyboard_neutral = true;
cube_dongle::HostBarrier barrier;
std::atomic<uint32_t> mailbox{0}, public_status{0};
std::atomic<uint8_t> bound_profiles{0};
std::atomic<uint32_t> next_ticket{0}, done_ticket{0};
std::atomic<int> command_result{0};
uint32_t pending_ticket = 0;
HostRequest pending;
ble_npl_callout output_timer;
bool timer_initialized = false;
bool transition = false, old_ble = false;
#if CONFIG_CUBE_STUDIO_RPC
std::mutex management_mutex;
struct ManagementSnapshot {
    Profile profiles[5];
    unsigned selected = 0;
    bool connected = false;
    Output output = Output::Auto;
} management;
std::array<std::string, 5> profile_names;
#endif
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
#if CONFIG_CUBE_STUDIO_RPC
void AuthorizeAfterPersistence();
void StudioReset() {
    std::lock_guard<std::mutex> lock(studio_mutex);
    studio_ready = false;
    const uint32_t epoch = studio_epoch.fetch_add(1) + 1;
    studio_pipe.Reset(epoch);
    studio_progress.Reset();
}
void StudioPublish() {
    const bool ready = authorized && Encrypted() && studio_subscribed && !transition;
    if (!ready && studio_ready.load())
        StudioReset();
    studio_ready = ready;
}
void StudioPump() {
    if (!studio_ready)
        return;
    uint8_t bytes[512];
    size_t size = 0;
    bool timed_out = false;
    {
        std::lock_guard<std::mutex> lock(studio_mutex);
        if (studio_pipe.Pending()) {
            timed_out = studio_progress.Expired(esp_timer_get_time());
        } else {
            size = studio_pipe.Next(bytes, ble_att_mtu(connection));
            if (size)
                timed_out = studio_progress.Expired(esp_timer_get_time());
        }
    }
    if (timed_out) {
        ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }
    if (!size)
        return;
    auto* mbuf = ble_hs_mbuf_from_flat(bytes, size);
    const int rc =
        mbuf ? ble_gatts_indicate_custom(connection, studio_handle, mbuf) : BLE_HS_ENOMEM;
    if (rc == BLE_HS_EBUSY || rc == BLE_HS_EAGAIN || rc == BLE_HS_ENOMEM) {
        std::lock_guard<std::mutex> lock(studio_mutex);
        studio_pipe.Retry();
    } else if (rc)
        ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
}
int StudioAccess(uint16_t conn, uint16_t, ble_gatt_access_ctxt* ctx, void*) {
    if (security_pending && conn == connection)
        AuthorizeAfterPersistence();
    if (conn != connection || !authorized || !Encrypted())
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (!studio_subscribed)
        return BLE_ATT_ERR_UNLIKELY;
    if (ctx->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    const size_t size = OS_MBUF_PKTLEN(ctx->om);
    uint8_t bytes[512];
    if (!size || size > sizeof(bytes) || os_mbuf_copydata(ctx->om, 0, size, bytes))
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    std::lock_guard<std::mutex> lock(studio_mutex);
    return studio_pipe.Receive(bytes, size) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
#endif
void Publish() {
    uint8_t mask = 0;
    for (unsigned i = 0; i < 5; ++i)
        if (profiles[i].bound)
            mask |= 1u << i;
    bound_profiles = mask;
#if CONFIG_CUBE_STUDIO_RPC
    StudioPublish();
    {
        std::lock_guard<std::mutex> lock(management_mutex);
        std::copy(std::begin(profiles), std::end(profiles), std::begin(management.profiles));
        management.selected = selected;
        management.connected = authorized && Encrypted();
        management.output = preference;
    }
#endif
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
// A fresh connection can expose the computer's rotating private address until
// identity resolution / SMP completes. Never persist this address as a profile.
bool UnresolvedPrivate(const ble_addr_t& address) {
    return address.type == BLE_ADDR_RANDOM && (address.val[5] & 0xc0) == 0x40;
}
void AuthorizeAfterPersistence() {
    ble_gap_conn_desc desc{};
    if (ble_gap_conn_find(connection, &desc) || !desc.sec_state.encrypted)
        return;  // The existing security deadline bounds every pending attempt.
    bool valid = desc.sec_state.bonded && ValidIdentity(desc.peer_id_addr) &&
                 !UnresolvedPrivate(desc.peer_id_addr) &&
                 !CubeSplitIsKeyboard(desc.peer_id_addr.val, desc.peer_id_addr.type & 1);
    ble_store_key_sec key{};
    ble_store_value_sec our{}, peer{};
    key.peer_addr = desc.peer_id_addr;
    key.peer_addr.type &= 1;
    // ENC_CHANGE precedes identity update and key persistence on fresh pairing.
    // Authorize only from a subsequent host event after both SC keys are stored.
    const bool persisted = valid && !ble_store_read_our_sec(&key, &our) && !ble_store_read_peer_sec(&key, &peer) &&
            our.ltk_present && peer.ltk_present && our.sc && peer.sc && our.key_size == 16 &&
            peer.key_size == 16;
    if (!persisted && esp_timer_get_time() < security_deadline)
        return;  // Retry on the host timer; never grant GATT/HID access early.
    security_pending = false;
    valid = persisted;
    for (unsigned i = 0; i < 5; ++i)
        if (i != selected && profiles[i].bound && Same(profiles[i].identity, desc.peer_id_addr))
            valid = false;
    if (valid && profiles[selected].bound)
        valid = Same(profiles[selected].identity, desc.peer_id_addr);
    else if (valid) {
        valid = esp_timer_get_time() < pairing_until && SaveProfile(selected, &desc.peer_id_addr);
        if (valid) {
            profiles[selected].bound = true;
            profiles[selected].identity = desc.peer_id_addr;
            profiles[selected].identity.type &= 1;
            pairing_until = 0;
        } else
            last_result = -1;
    }
    authorized = valid;
    ESP_LOGI(kTag, "BLE profile=%u authorization=%d persisted=%d", selected + 1, valid, persisted);
    ReleaseBle();
    if (routed_ble)
        barrier.Begin();
    if (valid)
        Notice(NotifyReady() ? 2 : 1);
    else {
        if (last_result.load() >= 0)
            last_result = -5;
        Notice(3);
        ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    }
}
void Advertise() {
    if (!synced || !configured || connection != BLE_HS_CONN_HANDLE_NONE || transition ||
        (!profiles[selected].bound && esp_timer_get_time() >= pairing_until))
        return;
    if (ble_gap_adv_active())
        return;
    const auto now = esp_timer_get_time();
    if (now < advertisement_next)
        return;
    advertisement_next = now + 250000;
    static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);
    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance = 0x03c1;
    fields.appearance_is_present = 1;
    fields.uuids16 = &hid_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
#if CONFIG_CUBE_STUDIO_RPC
    // 31-byte legacy advertising: flags 3 + appearance 4 + HID 4 + Studio 18 = 29.
    fields.uuids128 = &studio_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
#endif
    int rc = ble_gap_adv_set_fields(&fields);
    const char name[] = "Cube Velvet Pro";
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
        rc = ble_gap_adv_start(own_type, nullptr, BLE_HS_FOREVER, &params, Gap,
                               reinterpret_cast<void*>(++host_epoch));
    if (rc)
        last_result = rc;
}
void Pump() {
    if (!NotifyReady() || inflight)
        return;
    const auto* front = frames.Front();
    if (!front)
        return;
    while ((phase == 1 && (!protocol || !media_sub)) || (phase == 2 && (!protocol || !pointer_sub)))
        ++phase;
    if (phase == 3) {
        if (front->Neutral())
            neutral_sent = true;
        frames.Pop();
        phase = 0;
        return;
    }
    const auto pointer = cube_dongle::PointerBytes(front->pointer);
    const uint16_t handle = phase == 0   ? (protocol ? keyboard_handle : boot_handle)
                            : phase == 1 ? media_handle
                                         : pointer_handle;
    const void* bytes = phase == 0   ? static_cast<const void*>(front->keyboard.data())
                        : phase == 1 ? static_cast<const void*>(&front->media)
                                     : static_cast<const void*>(pointer.data());
    auto* mbuf = ble_hs_mbuf_from_flat(bytes, phase == 0 ? 8 : phase == 1 ? 1 : 7);
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
int Gap(ble_gap_event* event, void* arg) {
    if (reinterpret_cast<uintptr_t>(arg) != host_epoch)
        return 0;
    if (event->type == BLE_GAP_EVENT_CONNECT) {
        if (event->connect.status) {
            Advertise();
            return 0;
        }
        connection = event->connect.conn_handle;
#if CONFIG_CUBE_STUDIO_RPC
        studio_subscribed = false;
        StudioReset();
#endif
        authorized = false;
        security_pending = false;
        protocol = 1;
        suspended = false;
        keyboard_sub = media_sub = boot_sub = pointer_sub = false;
        ReleaseBle();
        ble_gap_conn_desc desc{};
        if (ble_gap_conn_find(connection, &desc) ||
            CubeSplitIsKeyboard(desc.peer_id_addr.val, desc.peer_id_addr.type & 1) ||
            (profiles[selected].bound && !Same(profiles[selected].identity, desc.peer_id_addr) &&
             !UnresolvedPrivate(desc.peer_id_addr)) ||
            (!profiles[selected].bound && esp_timer_get_time() >= pairing_until)) {
            ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        ESP_LOGI(kTag, "BLE profile=%u connected; securing", selected + 1);
        security_deadline = esp_timer_get_time() + 30000000;
        if (ble_gap_security_initiate(connection))
            ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT &&
               connection == event->disconnect.conn.conn_handle) {
        ESP_LOGI(kTag, "BLE profile=%u disconnected reason=%d", selected + 1,
                 event->disconnect.reason);
        connection = BLE_HS_CONN_HANDLE_NONE;
#if CONFIG_CUBE_STUDIO_RPC
        studio_subscribed = false;
        StudioReset();
#endif
        authorized = false;
        security_pending = false;
        keyboard_sub = media_sub = boot_sub = pointer_sub = false;
        inflight = false;
        phase = 0;
        ReleaseBle();
        if (routed_ble)
            barrier.Begin();
        Advertise();
    } else if (event->type == BLE_GAP_EVENT_ENC_CHANGE &&
               connection == event->enc_change.conn_handle) {
        ESP_LOGI(kTag, "BLE profile=%u encryption status=%d", selected + 1,
                 event->enc_change.status);
        authorized = false;
        security_pending = !event->enc_change.status;
        ReleaseBle();
        if (routed_ble)
            barrier.Begin();
        if (event->enc_change.status)
            ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    } else if (event->type == BLE_GAP_EVENT_SUBSCRIBE &&
               connection == event->subscribe.conn_handle) {
#if CONFIG_CUBE_STUDIO_RPC
        if (event->subscribe.attr_handle == studio_handle) {
            studio_subscribed = event->subscribe.cur_indicate;
            if (!studio_subscribed)
                StudioReset();
            Publish();
            return 0;
        }
#endif
        if (event->subscribe.attr_handle == keyboard_handle)
            keyboard_sub = event->subscribe.cur_notify;
        if (event->subscribe.attr_handle == media_handle)
            media_sub = event->subscribe.cur_notify;
        if (event->subscribe.attr_handle == pointer_handle)
            pointer_sub = event->subscribe.cur_notify;
        if (event->subscribe.attr_handle == boot_handle)
            boot_sub = event->subscribe.cur_notify;
        ReleaseBle();
        if (routed_ble)
            barrier.Begin();
        if (authorized)
            Notice(NotifyReady() ? 2 : 1);
    }
#if CONFIG_CUBE_STUDIO_RPC
    else if (event->type == BLE_GAP_EVENT_NOTIFY_TX && connection == event->notify_tx.conn_handle &&
             event->notify_tx.attr_handle == studio_handle && event->notify_tx.indication) {
        if (event->notify_tx.status == BLE_HS_EDONE) {
            std::lock_guard<std::mutex> lock(studio_mutex);
            studio_pipe.Confirm();
            studio_progress.Reset();
        } else if (event->notify_tx.status != 0)
            ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    }
#endif
    else if (event->type == BLE_GAP_EVENT_NOTIFY_TX && connection == event->notify_tx.conn_handle &&
             inflight && flight_handle == event->notify_tx.attr_handle &&
             !event->notify_tx.indication) {
        inflight = false;
        if (event->notify_tx.status) {
            ReleaseBle();
            phase = 0;
            last_result = event->notify_tx.status;
        } else if (flight_revision != revision)
            phase = 0;
        else if (++phase == 3) {
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
    RefOutput,
    Pointer,
    RefPointer
};
int Access(uint16_t conn, uint16_t, ble_gatt_access_ctxt* ctx, void* arg) {
    if (security_pending && conn == connection)
        AuthorizeAfterPersistence();
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
    const uint8_t ref[] = {static_cast<uint8_t>(field == RefPointer ? 3
                                                : field == RefMedia ? 2
                                                                    : 1),
                           static_cast<uint8_t>(field == RefOutput ? 2 : 1)};
    ReportFrame current = authorized && conn == connection ? frames.Latest() : ReportFrame{};
    current.pointer.x = current.pointer.y = 0;
    current.pointer.wheel = current.pointer.pan = 0;
    const auto pointer = cube_dongle::PointerBytes(current.pointer);
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
        case Pointer:
            bytes = pointer.data();
            size = pointer.size();
            break;
        case OutputLed:
            bytes = &leds;
            size = 1;
            break;
        case RefKeyboard:
        case RefMedia:
        case RefOutput:
        case RefPointer:
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
    static ble_gatt_dsc_def descriptors[4][2]{};
    for (unsigned i = 0; i < 4; ++i) {
        descriptors[i][0].uuid = &ref.u;
        descriptors[i][0].att_flags = BLE_ATT_F_READ;
        descriptors[i][0].access_cb = Access;
        descriptors[i][0].arg = reinterpret_cast<void*>(i == 3 ? RefPointer : RefKeyboard + i);
    }
    static ble_gatt_chr_def chars[11]{}, device_info[2]{};
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
    chars[9].uuid = &uuid[4].u;
    chars[9].access_cb = Access;
    chars[9].arg = reinterpret_cast<void*>(Pointer);
    chars[9].flags = read | BLE_GATT_CHR_F_NOTIFY;
    chars[9].val_handle = &pointer_handle;
    chars[9].descriptors = descriptors[3];
    device_info[0].uuid = &uuid[9].u;
    device_info[0].access_cb = Access;
    device_info[0].arg = reinterpret_cast<void*>(Pnp);
    device_info[0].flags = BLE_GATT_CHR_F_READ;
    static ble_gatt_svc_def services[4]{};
    services[0].type = services[1].type = BLE_GATT_SVC_TYPE_PRIMARY;
    services[0].uuid = &hid.u;
    services[0].characteristics = chars;
    services[1].uuid = &dis.u;
    services[1].characteristics = device_info;
#if CONFIG_CUBE_STUDIO_RPC
    static ble_gatt_chr_def studio_chars[2]{};
    studio_chars[0].uuid = &studio_char_uuid.u;
    studio_chars[0].access_cb = StudioAccess;
    studio_chars[0].flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
                            BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_INDICATE;
    studio_chars[0].val_handle = &studio_handle;
    services[2].type = BLE_GATT_SVC_TYPE_PRIMARY;
    services[2].uuid = &studio_uuid.u;
    services[2].characteristics = studio_chars;
#endif
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Cube Velvet Pro");
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
    return preference == Output::Ble || (preference == Output::Auto && !CubeUsbAttached());
}
bool OldReleased() {
    return old_ble ? (connection == BLE_HS_CONN_HANDLE_NONE || neutral_sent)
                   : (!CubeUsbAttached() || CubeUsbNeutralDelivered());
}
void BeginTransition(HostRequest request) {
#if CONFIG_CUBE_STUDIO_RPC
    CubeStudioCancelMacros();
#endif
    if (request.command == HostCommand::Next) {
        const bool usb_selected = preference == Output::Usb ||
                                  (preference == Output::Auto && !routed_ble);
        const auto slot = cube_dongle::NextHostCycleSlot(usb_selected, selected);
        request = slot == 0 ? HostRequest{HostCommand::Usb, 0}
                            : HostRequest{HostCommand::Select, static_cast<uint8_t>(slot - 1)};
    }
    if (request.command == HostCommand::Select && request.profile == selected &&
        preference == Output::Ble && routed_ble && profiles[selected].bound) {
        last_result = 0;
        command_result = 0;
        if (pending_ticket)
            done_ticket = pending_ticket;
        Notice(NotifyReady() ? 2 : 1);
        return;
    }
    transition_started = esp_timer_get_time();
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
        Notice(3);
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
        request.command == HostCommand::ClearAll || request.command == HostCommand::ClearSlot) {
        if (connection != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
            return;
        }
    }
    bool okay = SaveSelection(next_profile, next_output);
    if (okay && request.command == HostCommand::Clear)
        okay = ClearSlot(selected);
    if (okay && request.command == HostCommand::ClearSlot)
        okay = request.profile < 5 && ClearSlot(request.profile);
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
        if (!okay)
            Notice(3);
        else if (request.command == HostCommand::Usb)
            Notice(4);
        else if (request.command == HostCommand::Auto)
            Notice(5);
        else if (request.command == HostCommand::Disable)
            Notice(6);
        else if (request.command == HostCommand::Enable)
            Notice(7);
        else
            Notice(routed_ble && NotifyReady() ? 2 : 1);
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
#if CONFIG_CUBE_STUDIO_RPC
            char name[32]{};
            size_t name_size = sizeof(name);
            std::snprintf(key, sizeof(key), "n%u", i);
            if (nvs_get_str(handle, key, name, &name_size) == ESP_OK)
                profile_names[i] = name;
#endif
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
    CubeSplitInputTick();
    CubeDualTick();
    ble_npl_callout_reset(&output_timer, std::max<uint32_t>(1, ble_npl_time_ms_to_ticks32(5)));
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
    ble_npl_callout_reset(&output_timer, std::max<uint32_t>(1, ble_npl_time_ms_to_ticks32(5)));
    Advertise();
}
void CubeBleHostReset() {
    CubeUsbRelease();  // USB must release even while the shared radio host is down.
    physical_input = {};
    physical_neutral = keyboard_neutral = true;
#if CONFIG_CUBE_STUDIO_RPC
    CubeStudioCancelMacros();
    studio_subscribed = false;
    StudioReset();
#endif
    ++host_epoch;
    security_pending = false;
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
    if (security_pending && connection != BLE_HS_CONN_HANDLE_NONE)
        AuthorizeAfterPersistence();
#if CONFIG_CUBE_STUDIO_RPC
    StudioPublish();
    StudioPump();
    if (!enabled || (routed_ble ? !NotifyReady() : !CubeUsbAttached()))
        CubeStudioCancelMacros();
#endif
    if (connection != BLE_HS_CONN_HANDLE_NONE &&
        ((!authorized && esp_timer_get_time() >= security_deadline) ||
         (inflight && esp_timer_get_time() - inflight_since > 3000000)))
        ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    if (pending_system) {
        const bool released = system_old_ble
            ? (connection == BLE_HS_CONN_HANDLE_NONE || neutral_sent)
            : (!CubeUsbAttached() || CubeUsbNeutralDelivered());
        if (physical_neutral && released) {
            const auto action = pending_system;
            pending_system = 0;
            Application::GetInstance().Schedule([action]() {
                if (action == 1)
                    CubeUsbEnterDownload();
                else
                    Application::GetInstance().Reboot();
            });
        } else if (esp_timer_get_time() - system_started > 3000000) {
            pending_system = 0;
            ESP_LOGW(kTag, "System key cancelled: release keyboard and ensure host accepts neutral");
        }
    }
    if (!transition && !pending_system) {
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
    if (transition && old_ble && !NotifyReady() && connection != BLE_HS_CONN_HANDLE_NONE &&
        esp_timer_get_time() - transition_started >= 500000)
        ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    if (transition && OldReleased() && physical_neutral) {
        // Keep the request until the disconnect callback has completed.
        if ((pending.command == HostCommand::Select || pending.command == HostCommand::Clear ||
             pending.command == HostCommand::ClearAll ||
             pending.command == HostCommand::ClearSlot) &&
            connection != BLE_HS_CONN_HANDLE_NONE)
            ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
        else
            ApplyPending();
    }
    if (!transition && enabled &&
        barrier.Accept(
            physical_input,
            physical_neutral && (routed_ble ? (!NotifyReady() || neutral_sent)
                                            : (!CubeUsbAttached() || CubeUsbNeutralDelivered())))) {
        auto output = physical_input;
#if CONFIG_CUBE_STUDIO_RPC
        output = CubeStudioMacroOutput(output);
#endif
        if (routed_ble) {
            if (NotifyReady())
                SubmitBle(output);
        } else
            CubeUsbSubmit(output);
    }
    if (!profiles[selected].bound && esp_timer_get_time() >= pairing_until && ble_gap_adv_active())
        ble_gap_adv_stop();
    Advertise();
    Pump();
    Publish();
}
void CubeDualSubmit(const ReportFrame& frame, uint16_t actions) {
    const auto pointer = physical_input.pointer;
    physical_input = frame;
    physical_input.pointer = pointer;
    // Actual physical-neutral status is supplied separately, including layer keys.
    if (actions) {
#if CONFIG_CUBE_STUDIO_RPC
        if (actions & (1u << 9))
            CubeStudioUnlockUsb();
#endif
        if (!transition && !pending_system && (actions & ((1u << 8) | (1u << 10)))) {
            pending_system = (actions & (1u << 8)) ? 1 : 2;
            system_old_ble = routed_ble;
            system_started = esp_timer_get_time();
            CubeDualRequireNeutral();
#if CONFIG_CUBE_STUDIO_RPC
            CubeStudioCancelMacros();
#endif
        }
        HostRequest request;
        for (unsigned i = 0; i < 5; ++i)
            if (actions & (1u << i))
                request = {HostCommand::Select, static_cast<uint8_t>(i)};
        if (actions & 32)
            request = {HostCommand::Clear, 0};
        if (actions & 64)
            request = {HostCommand::ClearAll, 0};
        if (actions & 128)
            request = {HostCommand::Usb, 0};
        uint32_t expected = 0;
        const uint32_t ticket = (next_ticket.fetch_add(1) % 65535) + 1;
        if (request.command != HostCommand::None &&
            !mailbox.compare_exchange_strong(expected, static_cast<uint32_t>(request.command) |
                                                           (request.profile << 8) | (ticket << 16)))
            last_result = -2;
    }
    CubeDualTick();
}
void CubeDualSetNeutral(bool value) {
    keyboard_neutral = value;
    physical_neutral = value && !physical_input.pointer.buttons;
}
void CubeDualActions(uint16_t actions) { CubeDualSubmit(physical_input, actions); }
bool CubeDualPointer(const cube_dongle::PointerReport& pointer) {
    if (pointer.buttons & ~0x1fu)
        return false;
    physical_input.pointer = pointer;
    physical_neutral = keyboard_neutral && !pointer.buttons;
    const bool ready =
        enabled && !transition && !barrier.Waiting() &&
        (routed_ble ? NotifyReady() && protocol && pointer_sub : CubeUsbPointerReady());
    if (ready)
        CubeDualTick();
    // Relative motion is consumed once, never replayed by the 5ms timer.
    physical_input.pointer.x = physical_input.pointer.y = 0;
    physical_input.pointer.wheel = physical_input.pointer.pan = 0;
    return ready;
}
void CubeDualRequireNeutral() {
    barrier.Begin();
    CubeUsbRelease();
    ReleaseBle();
}
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
uint32_t CubeDualNotice() { return ui_notice.load(); }
#if CONFIG_CUBE_STUDIO_RPC
cube_dongle::studio::Bytes CubeBleManagement(const cube_dongle::studio::Bytes& payload,
                                             bool unlocked) {
    namespace wire = cube_dongle::studio;
    wire::Reader request(payload);
    const auto* op = request.Operation();
    if (!request.valid || !op || op->wire != 2)
        return wire::CustomError("Malformed BLE management request");
    wire::Reader args(op->data, op->size);
    if (!args.valid || !args.Numbers({1}))
        return wire::CustomError("Malformed BLE management arguments");
    wire::Bytes body, result;
    ManagementSnapshot snapshot;
    std::array<std::string, 5> names;
    {
        std::lock_guard<std::mutex> lock(management_mutex);
        snapshot = management;
        names = profile_names;
    }
    const uint64_t index = args.Value(1);
    if (op->tag == 1) {
        for (unsigned i = 0; i < 5; ++i) {
            wire::Bytes item;
            wire::Number(item, 1, i);
            wire::Text(item, 2, names[i]);
            if (snapshot.profiles[i].bound) {
                const auto* a = snapshot.profiles[i].identity.val;
                char address[18];
                std::snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4],
                              a[3], a[2], a[1], a[0]);
                wire::Text(item, 3, address);
            }
            wire::Number(item, 4, i == snapshot.selected && snapshot.connected);
            wire::Number(item, 5, !snapshot.profiles[i].bound);
            wire::Number(item, 6, i == snapshot.selected);
            wire::Message(body, 1, item);
        }
        wire::Number(body, 2, 5);
    } else if (op->tag == 5) {
        wire::Bytes info;
        wire::Number(info, 1, 1);
        wire::Number(info, 2, 1);
        wire::Number(info, 3, (CubeSplitBenchStatus() & 12) != 0);
        wire::Message(body, 1, info);
    } else if (op->tag == 8) {
        wire::Number(body, 1, snapshot.output == Output::Ble ? 1 : 0);
    } else {
        if (!unlocked)
            return wire::CustomError("Studio is locked");
        if ((op->tag == 2 || op->tag == 3 || op->tag == 4) && index >= 5)
            return wire::CustomError("Invalid profile index");
        bool okay = false;
        if (op->tag == 2) {
            const auto* name = args.Get(2);
            if (!name || name->wire != 2 || name->size > 31 ||
                std::memchr(name->data, 0, name->size))
                return wire::CustomError("Invalid profile name");
            const std::string value(reinterpret_cast<const char*>(name->data), name->size);
            nvs_handle_t handle;
            if (nvs_open("cube_hosts", NVS_READWRITE, &handle) == ESP_OK) {
                char key[4];
                std::snprintf(key, sizeof(key), "n%u", unsigned(index));
                okay = nvs_set_str(handle, key, value.c_str()) == ESP_OK &&
                       nvs_commit(handle) == ESP_OK;
                nvs_close(handle);
                if (okay) {
                    std::lock_guard<std::mutex> lock(management_mutex);
                    profile_names[index] = value;
                }
            }
        } else if (op->tag == 6) {
            okay = CubeSplitForgetBonds();
        } else if (op->tag == 3 || op->tag == 4 || op->tag == 7) {
            if (CubeSplitBenchStatus() >> 8)
                return wire::CustomError("Release keyboard keys before changing host");
            HostRequest action;
            if (op->tag == 3)
                action = {HostCommand::Select, uint8_t(index)};
            else if (op->tag == 4)
                action = {HostCommand::ClearSlot, uint8_t(index)};
            else if (index <= 1)
                action = {index ? HostCommand::Ble : HostCommand::Usb, 0};
            else
                return wire::CustomError("Invalid output priority");
            uint32_t expected = 0;
            const uint32_t ticket = (next_ticket.fetch_add(1) % 65535) + 1;
            const uint32_t message =
                uint32_t(action.command) | (action.profile << 8) | (ticket << 16);
            if (!mailbox.compare_exchange_strong(expected, message))
                return wire::CustomError("Host management busy");
            const int64_t deadline = esp_timer_get_time() + 2000000;
            while (done_ticket.load() != ticket && esp_timer_get_time() < deadline)
                vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(5)));
            if (done_ticket.load() != ticket)
                return wire::CustomError("Host change pending; read profiles before retrying");
            okay = command_result.load() == 0;
        } else
            return wire::CustomError("Unsupported BLE management operation");
        wire::Number(body, 1, okay);
    }
    wire::Message(result, op->tag + 1, body);
    return result;
}
bool CubeBleStudioReady() { return studio_ready.load(); }
uint32_t CubeBleStudioEpoch() { return studio_epoch.load(); }
size_t CubeBleStudioRead(uint8_t* bytes, size_t capacity, uint32_t& epoch) {
    std::lock_guard<std::mutex> lock(studio_mutex);
    epoch = studio_pipe.Epoch();
    return studio_ready.load() ? studio_pipe.Read(bytes, capacity) : 0;
}
bool CubeBleStudioWrite(const uint8_t* bytes, size_t size, uint32_t epoch) {
    std::lock_guard<std::mutex> lock(studio_mutex);
    return studio_ready.load() && studio_pipe.Write(bytes, size, epoch);
}
#endif
bool CubeDualEnabled() { return public_status.load() & 1; }
uint8_t CubeDualBoundProfiles() { return bound_profiles.load(); }
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

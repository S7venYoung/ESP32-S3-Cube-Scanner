#include "cube_split_bench.h"
#if CONFIG_CUBE_BLE_DONGLE_OUTPUT
#include "cube_ble_dongle.h"
#endif
#include "../../experimental/cube_dongle/typing_activity.h"
#include "../../experimental/cube_dongle/velvet_keymap.h"
#if CONFIG_CUBE_USB_DONGLE_OUTPUT
#include "cube_usb_dongle.h"
#endif
#include "../../experimental/cube_dongle/auto_binding.h"
#include "../../experimental/cube_dongle/peer_binding.h"
#include "../../experimental/cube_dongle/velvet_positions.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_sm.h>
#include <host/ble_store.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <nvs.h>
#include <os/os_mbuf.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

extern "C" void ble_store_config_init(void);

namespace {
constexpr char kTag[] = "CubeSplitBench";
const ble_uuid128_t kService = BLE_UUID128_INIT(0x2a, 0x48, 0xc2, 0xb1, 0xcf, 0xc5, 0x67, 0xc9,
                                                0x07, 0x71, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00);
const ble_uuid128_t kPosition = BLE_UUID128_INIT(0x2a, 0x48, 0xc2, 0xb1, 0xcf, 0xc5, 0x67, 0xc9,
                                                 0x07, 0x71, 0x96, 0x00, 0x01, 0x00, 0x00, 0x00);
enum class Phase {
    Idle,
    Connecting,
    Security,
    Identity,
    Service,
    Characteristic,
    Ccc,
    Reading,
    Ready,
    Closing
};
struct Peer {
    ble_addr_t address{};
    bool configured = false;
    bool auto_allowed = false, learning = false, identity_seen = false, identity_verified = false;
    bool auto_retry = false;
    cube_dongle::SplitState::Snapshot pending_positions{};
    uint16_t connection = BLE_HS_CONN_HANDLE_NONE;
    uintptr_t generation = 1;
    Phase phase = Phase::Idle;
    uint16_t service_start = 0, service_end = 0, value = 0, char_end = 0, ccc = 0;
    bool notified_during_read = false;
    int64_t retry_after = 0;
    ble_npl_callout timeout{};
    cube_dongle::TypingActivity typing;
    int battery = -1;
    int64_t battery_seen = -1, battery_next = 0;
    bool battery_pending = false;
};
Peer peers[2];
cube_dongle::AutoCandidate candidates[2];
std::mutex binding_mutex;
const ble_uuid128_t kIdentity = BLE_UUID128_INIT(0x43, 0x56, 0x50, 0x31, 0x6f, 0x37, 0x4d, 0x91,
                                                 0xa6, 0x2e, 0x88, 0x05, 0xd4, 0x73, 0x9b, 0xc1);
cube_dongle::SplitState input(cube_dongle::VelvetOwnership());
cube_dongle::KeyboardState<61, 3> keyboard(cube_dongle::VelvetKeymap());
void PublishKeyboard() {
    keyboard.Apply(input.Combined());
#if CONFIG_CUBE_USB_DONGLE_OUTPUT
#if CONFIG_CUBE_BLE_DONGLE_OUTPUT
    const auto positions = input.Combined();
    bool neutral = true;
    for (auto byte : positions)
        neutral &= byte == 0;
    CubeDualSetNeutral(neutral);
    CubeDualSubmit({keyboard.Report(), keyboard.MediaReport()}, keyboard.Actions());
#else
    CubeUsbSubmit({keyboard.Report(), keyboard.MediaReport()});
    if (keyboard.Actions())
        ESP_LOGW(kTag, "Host BLE profile command unsupported by USB-only candidate: %u",
                 keyboard.Actions());
#endif
#endif
}
ble_npl_callout retry;
ble_npl_callout telemetry_timer;
std::mutex telemetry_mutex;
FightTelemetry fight_cache;
KeyboardStatus keyboard_cache;
bool connecting = false;
bool callouts_initialized = false;
uint8_t own_address_type = 0;
std::atomic<uint32_t> status_word{0};

void PublishStatus() {
    unsigned pressed = 0;
    for (uint8_t byte : input.Combined())
        for (; byte; byte &= byte - 1)
            ++pressed;
    uint32_t word = pressed << 8;
    for (size_t side = 0; side < 2; ++side) {
        if (peers[side].configured)
            word |= 1u << side;
        if (peers[side].phase == Phase::Ready)
            word |= 1u << (side + 2);
    }
    status_word.store(word, std::memory_order_relaxed);
    const auto now = esp_timer_get_time();
    FightTelemetry fight;
    const bool left = peers[0].phase == Phase::Ready, right = peers[1].phase == Phase::Ready;
    fight.online = left || right;
    fight.left_wpm = left ? peers[0].typing.Wpm(now / 1000) : -1;
    fight.right_wpm = right ? peers[1].typing.Wpm(now / 1000) : -1;
    fight.left_battery =
        left && peers[0].battery_seen >= 0 && now - peers[0].battery_seen < 90000000
            ? peers[0].battery
            : -1;
    fight.right_battery =
        right && peers[1].battery_seen >= 0 && now - peers[1].battery_seen < 90000000
            ? peers[1].battery
            : -1;
    fight.modifiers = cube_dongle::UiModifiers(keyboard.Report()[0]);
    KeyboardStatus status;
    status.online = fight.online;
    const char* layers[] = {"BASE", "NVA", "SYS"};
    std::snprintf(status.layer, sizeof(status.layer), "%s", layers[keyboard.ActiveLayer()]);
    status.wpm = fight.online ? std::max(0, fight.left_wpm) + std::max(0, fight.right_wpm) : -1;
    status.modifiers = fight.modifiers;
    status.left_battery = fight.left_battery;
    status.right_battery = fight.right_battery;
#if CONFIG_CUBE_BLE_DONGLE_OUTPUT
    status.profile = CubeDualProfile() + 1;
    status.usb = !CubeDualUsesBle() && CubeUsbConnected() && CubeDualEnabled();
    status.ble = CubeDualUsesBle() && CubeDualBleReady() && CubeDualEnabled();
#elif CONFIG_CUBE_USB_DONGLE_OUTPUT
    status.usb = CubeUsbConnected() && CubeUsbEnabled();
#endif
    // Split links are not the computer's Bluetooth HID connection.
    std::lock_guard<std::mutex> lock(telemetry_mutex);
    fight_cache = fight;
    keyboard_cache = status;
}

int OnGap(ble_gap_event* event, void* arg);
void Scan(ble_npl_event*);

void Retry() { ble_npl_callout_reset(&retry, ble_npl_time_ms_to_ticks32(2000)); }
void* Cookie(size_t side) { return reinterpret_cast<void*>((peers[side].generation << 1) | side); }
Peer* Current(uint16_t conn, void* arg) {
    const auto cookie = reinterpret_cast<uintptr_t>(arg);
    Peer& peer = peers[cookie & 1];
    if ((cookie >> 1) != peer.generation || conn != peer.connection || peer.phase == Phase::Closing)
        return nullptr;
    return &peer;
}
size_t Side(const Peer& peer) { return static_cast<size_t>(&peer - peers); }
bool Encrypted(const Peer& peer) {
    ble_gap_conn_desc desc{};
    return ble_gap_conn_find(peer.connection, &desc) == 0 && desc.sec_state.encrypted &&
           std::memcmp(desc.peer_id_addr.val, peer.address.val, 6) == 0 &&
           (desc.peer_id_addr.type & 1) == peer.address.type;
}
void Fail(Peer& peer, const char* reason) {
    ESP_LOGW(kTag, "%s: %s", Side(peer) ? "right" : "left", reason);
    input.Disconnect(Side(peer));
    peer.typing.Reset();
    peer.battery = -1;
    peer.battery_seen = -1;
    PublishKeyboard();
    peer.phase = Phase::Closing;
    PublishStatus();
    ble_npl_callout_stop(&peer.timeout);
    const int rc = ble_gap_terminate(peer.connection, BLE_ERR_REM_USER_CONN_TERM);
    if (rc == BLE_HS_ENOTCONN) {
        peer.connection = BLE_HS_CONN_HANDLE_NONE;
        peer.phase = Phase::Idle;
        peer.learning = false;
        ++peer.generation;
        peer.retry_after = esp_timer_get_time() + 5000000;
        Retry();
    } else if (rc)
        ESP_LOGE(kTag, "Terminate failed: %d; peer remains quarantined", rc);
}
void Timeout(ble_npl_event* event) {
    auto& peer = *static_cast<Peer*>(ble_npl_event_get_arg(event));
    if (peer.connection != BLE_HS_CONN_HANDLE_NONE && peer.phase != Phase::Ready)
        Fail(peer, "handshake timeout");
}
bool Snapshot(Peer& peer, os_mbuf* mbuf) {
    uint8_t bytes[16];
    if (!mbuf || OS_MBUF_PKTLEN(mbuf) != sizeof(bytes) ||
        os_mbuf_copydata(mbuf, 0, sizeof(bytes), bytes) || !Encrypted(peer))
        return false;
    const auto owners = cube_dongle::VelvetOwnership();
    for (size_t i = 0; i < sizeof(bytes); ++i)
        if (bytes[i] & ~owners[Side(peer)][i])
            return false;
    if (peer.learning && !peer.configured) {
        std::memcpy(peer.pending_positions.data(), bytes, sizeof(bytes));
        return true;
    }
    if (!input.Apply(Side(peer), bytes, sizeof(bytes), true))
        return false;
    const auto state = input.Combined();
    PublishKeyboard();
    cube_dongle::SplitState::Snapshot owned{};
    std::memcpy(owned.data(), bytes, sizeof(bytes));
    peer.typing.Observe(owned, keyboard, esp_timer_get_time() / 1000);
    PublishStatus();
    unsigned pressed = 0;
    for (uint8_t byte : state)
        for (; byte; byte &= byte - 1)
            ++pressed;
    ESP_LOGD(kTag, "%s snapshot accepted, total pressed=%u", Side(peer) ? "right" : "left",
             pressed);
    return true;
}
int OnBattery(uint16_t conn, const ble_gatt_error* error, ble_gatt_attr* attr, void* arg) {
    Peer* peer = Current(conn, arg);
    if (!peer || peer->phase != Phase::Ready)
        return 0;
    uint8_t value;
    if (!error->status && attr && attr->om && OS_MBUF_PKTLEN(attr->om) == 1 &&
        !os_mbuf_copydata(attr->om, 0, 1, &value) && value <= 100) {
        peer->battery = value;
        peer->battery_seen = esp_timer_get_time();
        PublishStatus();
    }
    if (error->status)
        peer->battery_pending = false;
    return 0;
}
void TelemetryTick(ble_npl_event*) {
    const auto now = esp_timer_get_time();
    static const ble_uuid16_t battery_uuid = BLE_UUID16_INIT(0x2a19);
    for (size_t side = 0; side < 2; ++side) {
        auto& peer = peers[side];
        if (peer.phase == Phase::Ready && !peer.battery_pending && now >= peer.battery_next) {
            peer.battery_next = now + 30000000;
            peer.battery_pending = true;
            if (ble_gattc_read_by_uuid(peer.connection, 1, 0xffff, &battery_uuid.u, OnBattery,
                                       Cookie(side)))
                peer.battery_pending = false;
        }
    }
    PublishStatus();
    ble_npl_callout_reset(&telemetry_timer, ble_npl_time_ms_to_ticks32(100));
}
bool SaveAutoBinding(Peer& peer) {
    std::lock_guard<std::mutex> lock(binding_mutex);
    if (!peer.identity_verified || !Encrypted(peer))
        return false;
    ble_store_key_sec key{};
    key.peer_addr = peer.address;
    ble_store_value_sec ours{}, theirs{};
    if (ble_store_read_our_sec(&key, &ours) || ble_store_read_peer_sec(&key, &theirs) ||
        !ours.ltk_present || !theirs.ltk_present || !ours.sc || !theirs.sc)
        return false;
    nvs_handle_t handle;
    if (nvs_open("cube_split", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    const char* name = Side(peer) ? "right" : "left";
    char existing[20];
    size_t length = sizeof(existing);
    const int found = nvs_get_str(handle, name, existing, &length);
    // Never overwrite manual bindings or malformed persisted records.
    if (found != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return false;
    }
    char value[20];
    std::snprintf(value, sizeof(value), "%02X:%02X:%02X:%02X:%02X:%02X %c", peer.address.val[5],
                  peer.address.val[4], peer.address.val[3], peer.address.val[2],
                  peer.address.val[1], peer.address.val[0], '0' + (peer.address.type & 1));
    length = sizeof(existing);
    const int other = nvs_get_str(handle, Side(peer) ? "left" : "right", existing, &length);
    if ((other != ESP_OK && other != ESP_ERR_NVS_NOT_FOUND) ||
        (other == ESP_OK && !std::strcmp(existing, value))) {
        nvs_close(handle);
        return false;
    }
    auto rc = nvs_set_str(handle, name, value);
    if (rc == ESP_OK)
        rc = nvs_commit(handle);
    nvs_close(handle);
    return rc == ESP_OK;
}
int OnRead(uint16_t conn, const ble_gatt_error* error, ble_gatt_attr* attr, void* arg) {
    Peer* peer = Current(conn, arg);
    if (!peer || peer->phase != Phase::Reading)
        return 0;
    if (error->status || !attr || (!peer->notified_during_read && !Snapshot(*peer, attr->om))) {
        Fail(*peer, "initial position read failed");
        return 0;
    }
    if (peer->learning && !peer->configured) {
        if (!SaveAutoBinding(*peer)) {
            Fail(*peer, "automatic binding verification/storage failed");
            return 0;
        }
        peer->configured = true;
        peer->learning = false;
        peer->auto_retry = false;
#if CONFIG_CUBE_BLE_DONGLE_OUTPUT
        CubeDualRequireNeutral();
#elif CONFIG_CUBE_USB_DONGLE_OUTPUT
        CubeUsbRelease();
#endif
        input.Apply(Side(*peer), peer->pending_positions.data(), 16, true);
        PublishKeyboard();
        peer->typing.Observe(peer->pending_positions, keyboard, esp_timer_get_time() / 1000);
        ESP_LOGI(kTag, "%s automatic binding saved", Side(*peer) ? "right" : "left");
    }
    peer->phase = Phase::Ready;
    PublishStatus();
    ble_npl_callout_stop(&peer->timeout);
    ESP_LOGI(kTag, "%s ready, encrypted input only; HID OFF", Side(*peer) ? "right" : "left");
    return 0;
}
int OnSubscribed(uint16_t conn, const ble_gatt_error* error, ble_gatt_attr*, void* arg) {
    Peer* peer = Current(conn, arg);
    if (!peer || peer->phase != Phase::Ccc)
        return 0;
    if (error->status) {
        Fail(*peer, "CCC write failed");
        return 0;
    }
    peer->phase = Phase::Reading;
    if (ble_gattc_read(conn, peer->value, OnRead, arg))
        Fail(*peer, "cannot start initial read");
    return 0;
}
int OnDescriptor(uint16_t conn, const ble_gatt_error* error, uint16_t,
                 const ble_gatt_dsc* descriptor, void* arg) {
    Peer* peer = Current(conn, arg);
    if (!peer || peer->phase != Phase::Ccc)
        return 0;
    if (error->status == 0 && descriptor) {
        if (ble_uuid_u16(&descriptor->uuid.u) == 0x2902)
            peer->ccc = descriptor->handle;
    } else if (error->status == BLE_HS_EDONE && peer->ccc) {
        const uint8_t enable[] = {1, 0};
        if (ble_gattc_write_flat(conn, peer->ccc, enable, sizeof(enable), OnSubscribed, arg))
            Fail(*peer, "cannot subscribe");
    } else
        Fail(*peer, "position CCC not found");
    return 0;
}
int OnCharacteristic(uint16_t conn, const ble_gatt_error* error, const ble_gatt_chr* chr,
                     void* arg) {
    Peer* peer = Current(conn, arg);
    if (!peer || peer->phase != Phase::Characteristic)
        return 0;
    if (!error->status && chr) {
        if (ble_uuid_cmp(&chr->uuid.u, &kPosition.u) == 0) {
            if ((chr->properties & (BLE_GATT_CHR_PROP_READ | BLE_GATT_CHR_PROP_NOTIFY)) !=
                (BLE_GATT_CHR_PROP_READ | BLE_GATT_CHR_PROP_NOTIFY)) {
                Fail(*peer, "position characteristic lacks read/notify");
                return 0;
            }
            peer->value = chr->val_handle;
        } else if (peer->value && chr->def_handle > peer->value &&
                   chr->def_handle - 1 < peer->char_end) {
            peer->char_end = chr->def_handle - 1;
        }
    } else if (error->status == BLE_HS_EDONE && peer->value && peer->char_end > peer->value) {
        peer->phase = Phase::Ccc;
        if (ble_gattc_disc_all_dscs(conn, peer->value, peer->char_end, OnDescriptor, arg))
            Fail(*peer, "cannot discover CCC");
    } else
        Fail(*peer, "position characteristic not found");
    return 0;
}
int OnService(uint16_t conn, const ble_gatt_error* error, const ble_gatt_svc* service, void* arg) {
    Peer* peer = Current(conn, arg);
    if (!peer || peer->phase != Phase::Service)
        return 0;
    if (!error->status && service) {
        if (peer->service_start) {
            Fail(*peer, "ambiguous split services");
            return 0;
        }
        peer->service_start = service->start_handle;
        peer->service_end = service->end_handle;
    } else if (error->status == BLE_HS_EDONE && peer->service_start) {
        peer->phase = Phase::Characteristic;
        peer->char_end = peer->service_end;
        if (ble_gattc_disc_all_chrs(conn, peer->service_start, peer->service_end, OnCharacteristic,
                                    arg))
            Fail(*peer, "cannot discover characteristics");
    } else
        Fail(*peer, "split service not found");
    return 0;
}
void Discover(Peer& peer);
int OnIdentity(uint16_t conn, const ble_gatt_error* error, ble_gatt_attr* attr, void* arg) {
    Peer* peer = Current(conn, arg);
    if (!peer || peer->phase != Phase::Identity)
        return 0;
    if (!error->status && attr) {
        uint8_t data[18];
        if (peer->identity_seen || !attr->om || OS_MBUF_PKTLEN(attr->om) != 18 ||
            os_mbuf_copydata(attr->om, 0, 18, data) ||
            !cube_dongle::AutoIdentity(data, 18, Side(*peer))) {
            Fail(*peer, "invalid/ambiguous Cube side identity");
            return 0;
        }
        peer->identity_seen = true;
    } else if (error->status == BLE_HS_EDONE && peer->identity_seen) {
        peer->identity_verified = true;
        peer->auto_retry = true;
        Discover(*peer);
    } else
        Fail(*peer, "Cube side identity unavailable");
    return 0;
}
void Discover(Peer& peer) {
    if (!Encrypted(peer)) {
        Fail(peer, "encryption or peer identity mismatch");
        return;
    }
    if (peer.learning && !peer.identity_verified) {
        peer.phase = Phase::Identity;
        if (ble_gattc_read_by_uuid(peer.connection, 1, 0xffff, &kIdentity.u, OnIdentity,
                                   Cookie(Side(peer))))
            Fail(peer, "cannot read Cube side identity");
        return;
    }
    peer.phase = Phase::Service;
    if (ble_gattc_disc_svc_by_uuid(peer.connection, &kService.u, OnService, Cookie(Side(peer))))
        Fail(peer, "cannot discover split service");
}
void Connect(Peer& peer) {
    connecting = true;
    peer.phase = Phase::Connecting;
    peer.learning = !peer.configured;
    peer.identity_seen = peer.identity_verified = false;
    peer.pending_positions = {};
    ++peer.generation;
    ble_gap_disc_cancel();
    const int rc =
        ble_gap_connect(own_address_type, &peer.address, 10000, nullptr, OnGap, Cookie(Side(peer)));
    if (rc) {
        peer.phase = Phase::Idle;
        peer.learning = false;
        peer.retry_after = esp_timer_get_time() + 5000000;
        connecting = false;
        Retry();
    }
}
bool ConnectCandidate() {
    for (size_t side = 0; side < 2; ++side) {
        auto& peer = peers[side];
        const auto& candidate = candidates[side];
        if (peer.configured || !peer.auto_allowed || peer.phase != Phase::Idle || !candidate.seen ||
            candidate.ambiguous || esp_timer_get_time() < peer.retry_after)
            continue;
        if (peer.auto_retry && (peer.address.type != candidate.identity[0] ||
                                std::memcmp(peer.address.val, candidate.identity.data() + 1, 6)))
            continue;
        const auto& other = peers[side ^ 1];
        if ((other.configured || other.learning) && other.address.type == candidate.identity[0] &&
            !std::memcmp(other.address.val, candidate.identity.data() + 1, 6))
            continue;
        peer.address.type = candidate.identity[0];
        std::memcpy(peer.address.val, candidate.identity.data() + 1, 6);
        Connect(peer);
        return true;
    }
    return false;
}
int OnGap(ble_gap_event* event, void* arg) {
    if (event->type == BLE_GAP_EVENT_DISC) {
        // Known bonded halves use directed advertisements without service data.
        if (connecting || event->disc.event_type > 1)
            return 0;
        for (size_t side = 0; side < 2; ++side) {
            auto& peer = peers[side];
            if ((!peer.configured && !peer.auto_retry) || peer.phase != Phase::Idle ||
                esp_timer_get_time() < peer.retry_after ||
                peer.address.type != event->disc.addr.type ||
                std::memcmp(peer.address.val, event->disc.addr.val, 6))
                continue;
            Connect(peer);
            return 0;
        }
        // Learn only undirected Cube-specific advertisements, never generic ZMK.
        if (event->disc.event_type != 0 || event->disc.addr.type > 1)
            return 0;
        for (const auto& peer : peers)
            if (peer.configured && peer.address.type == event->disc.addr.type &&
                !std::memcmp(peer.address.val, event->disc.addr.val, 6))
                return 0;
        const int side = cube_dongle::AutoSide(event->disc.data, event->disc.length_data);
        if (side >= 0 && peers[side].auto_allowed && !peers[side].configured) {
            char address[18];
            const auto* b = event->disc.addr.val;
            std::snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X", b[5], b[4],
                          b[3], b[2], b[1], b[0]);
            cube_dongle::PeerBinding identity;
            if (cube_dongle::ParseIdentity(address, event->disc.addr.type, identity))
                candidates[side].Observe(b, event->disc.addr.type);
        }
        return 0;
    }
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        if (!connecting && !ConnectCandidate())
            Retry();
        return 0;
    }
    const auto cookie = reinterpret_cast<uintptr_t>(arg);
    auto& peer = peers[cookie & 1];
    if ((cookie >> 1) != peer.generation)
        return 0;
    if (peer.phase == Phase::Closing && event->type != BLE_GAP_EVENT_DISCONNECT)
        return 0;
    if (event->type == BLE_GAP_EVENT_CONNECT) {
        connecting = false;
        if (event->connect.status) {
            peer.phase = Phase::Idle;
            peer.learning = false;
            peer.retry_after = esp_timer_get_time() + 5000000;
        } else {
            peer.connection = event->connect.conn_handle;
            peer.phase = Phase::Security;
            peer.service_start = peer.service_end = peer.value = peer.ccc = 0;
            peer.notified_during_read = false;
            ble_npl_callout_reset(&peer.timeout, ble_npl_time_ms_to_ticks32(15000));
            if (Encrypted(peer))
                Discover(peer);
            else if (ble_gap_security_initiate(peer.connection))
                Fail(peer, "cannot initiate security");
        }
        Retry();
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT &&
               peer.connection == event->disconnect.conn.conn_handle) {
        input.Disconnect(cookie & 1);
        peer.typing.Reset();
        peer.battery = -1;
        peer.battery_seen = -1;
        peer.battery_next = 0;
        peer.battery_pending = false;
        PublishKeyboard();
        ble_npl_callout_stop(&peer.timeout);
        peer.connection = BLE_HS_CONN_HANDLE_NONE;
        peer.phase = Phase::Idle;
        peer.learning = false;
        ++peer.generation;
        peer.retry_after = esp_timer_get_time() + 5000000;
        ESP_LOGI(kTag, "%s disconnected; its positions released", (cookie & 1) ? "right" : "left");
        PublishStatus();
        Retry();
    } else if (event->type == BLE_GAP_EVENT_ENC_CHANGE &&
               peer.connection == event->enc_change.conn_handle) {
        if (event->enc_change.status || !Encrypted(peer))
            Fail(peer, "security failed; bonds NOT deleted");
        else if (peer.phase == Phase::Security)
            Discover(peer);
    } else if (event->type == BLE_GAP_EVENT_NOTIFY_RX &&
               peer.connection == event->notify_rx.conn_handle &&
               event->notify_rx.attr_handle == peer.value &&
               (peer.phase == Phase::Ready || peer.phase == Phase::Reading)) {
        if (!Snapshot(peer, event->notify_rx.om))
            Fail(peer, "invalid position notification");
        else if (peer.phase == Phase::Reading)
            peer.notified_during_read = true;
    } else if (event->type == BLE_GAP_EVENT_REPEAT_PAIRING) {
        return BLE_GAP_REPEAT_PAIRING_IGNORE;  // Never silently erase working bonds.
    }
    return 0;
}
void Scan(ble_npl_event*) {
    if (connecting || ble_gap_disc_active())
        return;
    bool wanted = false;
    for (const auto& peer : peers)
        wanted |= (peer.configured || peer.auto_allowed) && peer.phase == Phase::Idle;
    if (!wanted)
        return;
    ble_gap_disc_params params{};
    params.passive = 1;
    params.itvl = 0x100;
    params.window = 0x30;
    params.filter_duplicates = 1;
    candidates[0] = {};
    candidates[1] = {};
    if (ble_gap_disc(own_address_type, 5000, &params, OnGap, nullptr))
        Retry();
}
bool Configure(Peer& peer, const char* address, int type) {
    cube_dongle::PeerBinding binding;
    if (!cube_dongle::ParseIdentity(address, type, binding))
        return false;
    peer.address.type = type;
    std::memcpy(peer.address.val, binding.bytes.data(), 6);
    peer.configured = true;
    return true;
}
void LoadBinding(Peer& peer, const char* key, const char* fallback, int fallback_type) {
    nvs_handle_t handle;
    char value[20];
    size_t size = sizeof(value);
    const auto opened = nvs_open("cube_split", NVS_READONLY, &handle);
    if (opened == ESP_OK) {
        const int rc = nvs_get_str(handle, key, value, &size);
        nvs_close(handle);
        if (rc == ESP_OK) {
            if (size == 20 && value[17] == ' ' && (value[18] == '0' || value[18] == '1')) {
                value[17] = 0;
                Configure(peer, value, value[18] - '0');
            }
            return;  // Invalid persisted identity never silently falls back to another peer.
        }
        if (rc != ESP_ERR_NVS_NOT_FOUND)
            return;
    } else if (opened != ESP_ERR_NVS_NOT_FOUND)
        return;
    Configure(peer, fallback, fallback_type);
#if CONFIG_CUBE_SPLIT_AUTO_BIND
    peer.auto_allowed = !peer.configured && fallback[0] == '\0';
#endif
}
void OnSync() {
    if (ble_hs_util_ensure_addr(0) || ble_hs_id_infer_auto(0, &own_address_type))
        return;
#if CONFIG_CUBE_BLE_DONGLE_OUTPUT
    CubeBleHostSync(own_address_type);
#endif
    if (!callouts_initialized) {
        ble_npl_callout_init(&retry, nimble_port_get_dflt_eventq(), Scan, nullptr);
        ble_npl_callout_init(&telemetry_timer, nimble_port_get_dflt_eventq(), TelemetryTick,
                             nullptr);
        for (auto& peer : peers)
            ble_npl_callout_init(&peer.timeout, nimble_port_get_dflt_eventq(), Timeout, &peer);
        callouts_initialized = true;
    }
    TelemetryTick(nullptr);
    Scan(nullptr);
}
void OnReset(int reason) {
#if CONFIG_CUBE_BLE_DONGLE_OUTPUT
    CubeBleHostReset();
#endif
    ESP_LOGE(kTag, "NimBLE reset %d; all positions released", reason);
    connecting = false;
    if (callouts_initialized) {
        ble_npl_callout_stop(&retry);
        ble_npl_callout_stop(&telemetry_timer);
    }
    for (size_t side = 0; side < 2; ++side) {
        input.Disconnect(side);
        peers[side].typing.Reset();
        peers[side].battery = -1;
        peers[side].battery_seen = -1;
        peers[side].battery_pending = false;
        peers[side].battery_next = 0;
        if (callouts_initialized)
            ble_npl_callout_stop(&peers[side].timeout);
        peers[side].connection = BLE_HS_CONN_HANDLE_NONE;
        peers[side].phase = Phase::Idle;
        peers[side].learning = false;
        ++peers[side].generation;
    }
    PublishKeyboard();
    PublishStatus();
}
void Host(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}
}  // namespace

void StartCubeSplitBench() {
    static bool started = false;
    if (started)
        return;
    started = true;
    LoadBinding(peers[0], "left", CONFIG_CUBE_SPLIT_LEFT_ADDRESS,
                CONFIG_CUBE_SPLIT_LEFT_ADDRESS_TYPE);
    LoadBinding(peers[1], "right", CONFIG_CUBE_SPLIT_RIGHT_ADDRESS,
                CONFIG_CUBE_SPLIT_RIGHT_ADDRESS_TYPE);
    PublishStatus();
    if (peers[0].configured && peers[1].configured &&
        std::memcmp(&peers[0].address, &peers[1].address, sizeof(ble_addr_t)) == 0) {
        ESP_LOGE(kTag, "Same identity for both halves; bench disabled");
        return;
    }
    ESP_LOGW(kTag, "Bench input only, USB/BLE HID OFF. Allowlist: left=%d right=%d",
             peers[0].configured, peers[1].configured);
#if !CONFIG_CUBE_BLE_DONGLE_OUTPUT
    if (!peers[0].configured && !peers[1].configured && !peers[0].auto_allowed &&
        !peers[1].auto_allowed) {
        ESP_LOGW(kTag,
                 "Configure fixed public/static identity addresses before testing; no automatic "
                 "pairing");
        return;
    }
#endif
    if (nimble_port_init() != ESP_OK) {
        ESP_LOGE(kTag, "NimBLE init failed");
        return;
    }
#if CONFIG_CUBE_BLE_DONGLE_OUTPUT
    if (!CubeBleHostInit()) {
        ESP_LOGE(kTag, "Computer HID/NVS initialization failed; no split input started");
        return;
    }
#endif
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;  // ZMK split uses encrypted L2, not a passkey UI.
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sync_cb = OnSync;
    ble_hs_cfg.reset_cb = OnReset;
    ble_store_config_init();
#if CONFIG_CUBE_BLE_DONGLE_OUTPUT
    // Never evict a split or another computer bond when the table is full.
    ble_hs_cfg.store_status_cb = [](ble_store_status_event*, void*) { return BLE_HS_ENOMEM; };
#endif
    nimble_port_freertos_init(Host);
}

uint32_t CubeSplitBenchStatus() { return status_word.load(std::memory_order_relaxed); }
bool CubeSplitIsKeyboard(const uint8_t* address, uint8_t type) {
    for (const auto& peer : peers)
        if ((peer.configured || peer.learning) && peer.address.type == type &&
            !std::memcmp(address, peer.address.val, 6))
            return true;
    return false;
}
FightTelemetry CubeSplitFightTelemetry() {
    std::lock_guard<std::mutex> lock(telemetry_mutex);
    return fight_cache;
}
KeyboardStatus CubeSplitKeyboardStatus() {
    std::lock_guard<std::mutex> lock(telemetry_mutex);
    return keyboard_cache;
}

std::string CubeSplitBenchBind(const std::string& command) {
    cube_dongle::BindRequest request;
    if (!cube_dongle::ParseBindCommand(command, request))
        return "ERR INVALID-BIND";
    std::lock_guard<std::mutex> lock(binding_mutex);
    nvs_handle_t handle;
    if (nvs_open("cube_split", NVS_READWRITE, &handle) != ESP_OK)
        return "ERR NVS-OPEN";
    char other[20];
    size_t size = sizeof(other);
    const int rc = nvs_get_str(handle, request.side ? "left" : "right", other, &size);
    cube_dongle::PeerBinding other_identity;
    bool duplicate = false;
    if (rc == ESP_OK && size == 20 && other[17] == ' ') {
        other[17] = 0;
        duplicate = cube_dongle::ParseIdentity(other, other[18] - '0', other_identity) &&
                    request.identity.bytes == other_identity.bytes &&
                    request.identity.type == other_identity.type;
    }
    if (duplicate) {
        nvs_close(handle);
        return "ERR SAME-PEER";
    }
    if (rc != ESP_OK && rc != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return "ERR NVS-READ";
    }
    const int write = nvs_set_str(handle, request.side ? "right" : "left", request.stored.c_str());
    const int commit = write == ESP_OK ? nvs_commit(handle) : write;
    nvs_close(handle);
#if CONFIG_CUBE_USB_DONGLE_OUTPUT
    return commit == ESP_OK ? "CUBE-SPLIT-BIND SAVED REBOOT-REQUIRED HID-MODE-UNCHANGED"
                            : "ERR NVS-WRITE";
#else
    return commit == ESP_OK ? "CUBE-SPLIT-BIND SAVED REBOOT-REQUIRED HID-OFF" : "ERR NVS-WRITE";
#endif
}

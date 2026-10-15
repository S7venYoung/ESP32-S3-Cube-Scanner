#include "cube_studio.h"
#include <esp_app_desc.h>
#include <esp_flash.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <atomic>
#include <cstdio>
#include <deque>
#include <mutex>
#include "../../experimental/cube_dongle/custom_settings.h"
#include "../../experimental/cube_dongle/default_layers.h"
#include "../../experimental/cube_dongle/fast_keymap.h"
#include "../../experimental/cube_dongle/peripheral_rpc.h"
#include "../../experimental/cube_dongle/runtime_macros.h"
#include "../../experimental/cube_dongle/runtime_pointer.h"
#include "../../experimental/cube_dongle/runtime_sensors.h"
#include "../../experimental/cube_dongle/studio_diagnostics.h"
#include "../../experimental/cube_dongle/studio_protocol.h"
#include "../../experimental/cube_dongle/studio_watchdog.h"
#include "application.h"
#include "cube_ble_dongle.h"
#include "cube_split_bench.h"
#include "cube_usb_dongle.h"

namespace {
namespace wire = cube_dongle::studio;
wire::Rpc rpc;
wire::CustomSettings custom_settings;
std::atomic<unsigned> tapping_term{200};
wire::DefaultLayers defaults, default_cache;
wire::PointerSettings pointers;
wire::PointerEngine pointer_engine;
std::array<wire::ProcessorConfig, 4> pointer_cache{};
std::mutex pointer_mutex;
std::atomic<uint8_t> pointer_hardware{0}, pointer_inertia{0};
void PointerNotifyStatus();
wire::SensorSettings sensors, sensor_cache;
wire::SensorEngine sensor_engine;
wire::SensorEngine::Result sensor_output;
std::mutex sensor_mutex;
std::atomic<uint8_t> sensor_hardware{0};
unsigned ActiveEndpoint() { return CubeDualUsesBle() ? unsigned(CubeDualProfile() + 1) : 0; }
size_t active_transport = 0;
wire::Bytes PeripheralRequest(const std::string& subsystem, const wire::Bytes& payload,
                              bool unlocked);
bool RelaySettings(unsigned source, uint32_t idle, uint32_t sleep, bool change);
void ProcessRelayEvents();
wire::RuntimeMacros macros;
wire::RuntimeCombos combos;
wire::ComboEngine combo_engine;
wire::ComboModel combo_cache;
std::mutex combo_mutex;
uint32_t combo_revision = 1, combo_seen = 0;
wire::WatchdogLog watchdog_log;
bool watchdog_subscribed = false;
bool SaveRecord(const char* key, const wire::Bytes& bytes) {
    nvs_handle_t handle;
    if (nvs_open("cube_studio", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    auto result = nvs_set_blob(handle, key, bytes.data(), bytes.size());
    if (result == ESP_OK)
        result = nvs_commit(handle);
    nvs_close(handle);
    return result == ESP_OK;
}
uint32_t ResetBits() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:
            return 1u << 3;
        case ESP_RST_EXT:
            return 1u << 0;
        case ESP_RST_SW:
            return 1u << 1;
        case ESP_RST_PANIC:
            return 1u << 8;
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            return 1u << 4;
        case ESP_RST_DEEPSLEEP:
            return 1u << 7;
        case ESP_RST_BROWNOUT:
            return 1u << 2;
        default:
            return 1u << 12;
    }
}
wire::MacroPlayer macro_player;
wire::MacroModel macro_cache;
std::mutex macro_mutex;
std::array<bool, 61> macro_keys{};
std::atomic<uint32_t> activity_idle{0}, activity_sleep{0};
std::atomic<int64_t> activity_last{0};
std::mutex activity_mutex;
std::function<void(unsigned)> activity_handler;
unsigned activity_state = 0;
std::vector<wire::Bytes> pending_notifications;
void QueueActivityNotification() {
    if (pending_notifications.size() >= 8)
        return;
    size_t index = 0;
    while (index < rpc.custom.size() && rpc.custom[index].identifier != "zmk__settings")
        ++index;
    if (index == rpc.custom.size())
        return;
    wire::Bytes settings, event, payload, custom_event, custom_notification, notification, response;
    wire::Number(settings, 1, activity_idle.load());
    wire::Number(settings, 2, activity_sleep.load());
    wire::Number(settings, 3, 0);
    wire::Message(event, 1, settings);
    wire::Message(payload, 1, event);
    wire::Number(custom_event, 1, index);
    wire::Message(custom_event, 2, payload);
    wire::Message(custom_notification, 1, custom_event);
    wire::Message(notification, 100, custom_notification);
    wire::Message(response, 2, notification);
    pending_notifications.push_back(std::move(response));
}
wire::Bytes ActivitySettings(const wire::Bytes& payload, bool unlocked) {
    wire::Reader request(payload);
    const auto* op = request.Operation();
    if (!request.valid || !op || op->wire != 2)
        return wire::CustomError("Invalid activity settings request");
    wire::Bytes body, response;
    if (op->tag == 1) {
        wire::Bytes settings;
        wire::Number(settings, 1, activity_idle.load());
        wire::Number(settings, 2, activity_sleep.load());
        wire::Number(settings, 3, 0);
        wire::Message(body, 1, settings);
    } else if (op->tag == 2) {
        if (!unlocked)
            return wire::CustomError("Studio is locked");
        wire::Reader args(op->data, op->size);
        const auto* f = args.Get(1);
        if (!args.valid || !f || f->wire != 2)
            return wire::CustomError("Missing activity settings");
        wire::Reader settings(f->data, f->size);
        const auto idle = settings.Value(1), sleep = settings.Value(2);
        if (!settings.valid || !settings.Numbers({1, 2, 3}) || settings.Value(3) > 2 ||
            idle > 86400000 || sleep > 86400000 || (idle && idle < 1000) ||
            (sleep && sleep < 1000) || (idle && sleep && sleep < idle))
            return wire::CustomError("Invalid timeout or unavailable peripheral source");
        if (settings.Value(3)) {
            wire::Number(body, 1, RelaySettings(settings.Value(3), idle, sleep, true));
            wire::Message(response, 3, body);
            return response;
        }
        const uint32_t values[] = {uint32_t(idle), uint32_t(sleep)};
        nvs_handle_t handle;
        bool okay = false;
        if (nvs_open("cube_studio", NVS_READWRITE, &handle) == ESP_OK) {
            okay = nvs_set_blob(handle, "activity_v1", values, sizeof(values)) == ESP_OK &&
                   nvs_commit(handle) == ESP_OK;
            nvs_close(handle);
        }
        if (okay) {
            activity_idle = idle;
            activity_sleep = sleep;
            activity_last = esp_timer_get_time();
            QueueActivityNotification();
        }
        wire::Number(body, 1, okay);
    } else if (op->tag == 3) {
        QueueActivityNotification();
        for (unsigned source = 1; source <= 2; ++source)
            RelaySettings(source, 0, 0, false);
        wire::Number(body, 1, 1);
    } else
        return wire::CustomError("Unknown activity settings operation");
    wire::Message(response, op->tag + 1, body);
    return response;
}
bool SaveMacros(const wire::Bytes& bytes) {
    nvs_handle_t handle;
    if (nvs_open("cube_studio", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    auto result = nvs_set_blob(handle, "macros_v1", bytes.data(), bytes.size());
    if (result == ESP_OK)
        result = nvs_commit(handle);
    nvs_close(handle);
    return result == ESP_OK;
}
wire::Diagnostics diagnostics;
std::mutex diagnostics_mutex;
wire::Bytes DeviceInfo(const wire::Bytes& payload, bool) {
    wire::Reader request(payload);
    const auto* op = request.Operation();
    if (!request.valid || !op || op->tag != 1 || op->wire != 2)
        return wire::CustomError("Invalid device info request");
    const auto* app = esp_app_get_description();
    wire::Bytes build, hardware, config, runtime, body, response;
    wire::Text(build, 1, "Cube ESP-IDF / ZMK-compatible RPC");
    wire::Text(build, 3, app->version);
    wire::Text(build, 5, "Cube DYA adapter");
    wire::Text(build, 7, app->idf_ver);
    wire::Text(build, 8, std::string(app->date) + " " + app->time);
    wire::Text(build, 9, "nologo-xingzhi-cube-1.54tft-wifi");
    char build_hash[65];
    for (size_t i = 0; i < 32; ++i)
        std::snprintf(build_hash + i * 2, 3, "%02x", app->app_elf_sha256[i]);
    wire::Text(build, 10, build_hash);
    wire::Text(hardware, 1, rpc.serial);
    wire::Number(hardware, 2, esp_reset_reason());
    uint32_t flash_size = 0;
    if (esp_flash_get_size(nullptr, &flash_size) == ESP_OK)
        wire::Number(hardware, 3, flash_size / 1024);
    wire::Text(config, 1, "Cube remote BLE split positions");
    for (auto field : {2u, 4u, 5u, 7u, 10u})
        wire::Number(config, field, 1);
    wire::Number(config, 3, 5);
    wire::Text(config, 6, "central");
    wire::Number(runtime, 1, uint32_t(esp_timer_get_time() / 1000));
    wire::Message(body, 1, build);
    wire::Message(body, 2, hardware);
    wire::Message(body, 4, config);
    wire::Message(body, 5, runtime);
    wire::Message(response, 2, body);
    return response;
}
struct Session {
    wire::Framer framer;
    bool connected = false, unlocked = false, used = false, stream = false;
    bool tx_failed = false;
    int64_t until = 0, last_byte = 0;
    uint32_t epoch = 0;
};
std::array<Session, 2> sessions;
std::once_flag initialize_once;
std::mutex cache_mutex;
cube_dongle::KeyboardState<61, 4>::Keymap live_map = cube_dongle::VelvetStudioKeymap();
std::array<std::string, 4> live_names{{"BASE", "NVA", "SYS", "Layer4"}};
std::array<size_t, 4> live_order{{0, 1, 2, 3}};
size_t live_count = 3;
std::atomic<bool> unlock_request{false};
bool Save(const wire::Bytes& data) {
    nvs_handle_t handle;
    if (nvs_open("cube_studio", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    auto rc = nvs_set_blob(handle, "keymap_v1", data.data(), data.size());
    if (rc == ESP_OK)
        rc = nvs_commit(handle);
    nvs_close(handle);
    return rc == ESP_OK;
}
void UpdateCache() {
    std::lock_guard<std::mutex> lock(cache_mutex);
    live_map = rpc.model.map;
    live_names = rpc.model.names;
    live_order = rpc.model.order;
    live_count = rpc.model.count;
}
void Send(size_t transport, const wire::Bytes& payload) {
    // One stalled recipient must not block once per notification and exceed
    // the task watchdog. A fresh request (or connection epoch) permits retry.
    if (sessions[transport].tx_failed)
        return;
    const auto bytes = wire::Frame(payload);
    const bool sent =
        transport == 0 ? CubeCdcWritePacket(bytes.data(), bytes.size(), sessions[transport].epoch)
                       : CubeBleStudioWrite(bytes.data(), bytes.size(), sessions[transport].epoch);
    if (!sent) {
        sessions[transport].tx_failed = true;
        ESP_LOGW("CubeStudio", "Response interrupted; reconnect Studio");
    }
}
struct RelayEvent {
    unsigned source;
    std::string name;
    wire::Bytes data;
};
std::mutex relay_incoming_mutex;
std::deque<RelayEvent> relay_incoming;
std::vector<wire::PeripheralPending> relay_pending;
uint32_t relay_id = 1;
struct TargetNotification {
    unsigned transport;
    uint32_t epoch;
    wire::Bytes body;
};
std::deque<TargetNotification> relay_notifications;
std::mutex stream_mutex;
std::deque<TargetNotification> stream_notifications;
std::deque<TargetNotification> status_notifications;
std::atomic<uint8_t> stream_mask{0};
std::atomic<uint8_t> notification_targets{0};
std::atomic<bool> initialized_for_notifications{false};
std::array<std::atomic<uint32_t>, 2> stream_epochs{};
std::atomic<bool> stream_overflow{false};
void PublishStreamSessions() {
    uint8_t mask = 0;
    uint8_t targets = 0;
    const auto now = esp_timer_get_time();
    for (size_t i = 0; i < sessions.size(); ++i) {
        const auto& session = sessions[i];
        stream_epochs[i] = session.epoch;
        if (session.connected && session.used)
            targets |= 1u << i;
        if (session.connected && session.used && session.stream && session.unlocked &&
            now < session.until)
            mask |= 1u << i;
    }
    stream_mask = mask;
    notification_targets = targets;
}
wire::Bytes CustomNotification(const std::string& subsystem, const wire::Bytes& payload) {
    size_t index = 0;
    while (index < rpc.custom.size() && rpc.custom[index].identifier != subsystem)
        ++index;
    if (index == rpc.custom.size())
        return {};
    wire::Bytes event, custom, notification, response;
    wire::Number(event, 1, index);
    wire::Message(event, 2, payload);
    wire::Message(custom, 1, event);
    wire::Message(notification, 100, custom);
    wire::Message(response, 2, notification);
    return response;
}
void RelayNotify(const wire::PeripheralPending& pending, const wire::Bytes& payload) {
    if (relay_notifications.size() < 16)
        relay_notifications.push_back(
            {pending.transport, pending.epoch, CustomNotification(pending.subsystem, payload)});
}
void QueueStatus(const std::string& subsystem, const wire::Bytes& payload) {
    if (!initialized_for_notifications.load())
        return;
    const auto mask = notification_targets.load();
    if (!mask)
        return;
    const auto body = CustomNotification(subsystem, payload);
    std::lock_guard<std::mutex> lock(stream_mutex);
    for (unsigned i = 0; i < 2; ++i)
        if ((mask & (1u << i)) && status_notifications.size() < 32)
            status_notifications.push_back({i, stream_epochs[i].load(), body});
}
void PointerNotifyStatus() {
    static uint8_t previous = 0, fast = 0;
    const auto current = pointer_engine.Inertia(), next_fast = pointer_engine.Fast();
    for (unsigned i = 0; i < 4; ++i) {
        bool enabled = false;
        {
            std::lock_guard<std::mutex> lock(pointer_mutex);
            enabled = pointer_cache[i].values[24];
        }
        if (!enabled)
            continue;
        if (bool(previous & (1u << i)) != bool(current & (1u << i))) {
            wire::Bytes body, notification;
            wire::Number(body, 1, i);
            wire::Number(body, 2, bool(current & (1u << i)));
            wire::Number(body, 3, (current & (1u << i)) ? 0 : pointer_engine.StopReason(i));
            wire::Message(notification, 2, body);
            QueueStatus("cormoran_rip", notification);
        }
        if (bool(fast & (1u << i)) != bool(next_fast & (1u << i))) {
            wire::Bytes body, notification;
            wire::Number(body, 1, i);
            wire::Number(body, 2, bool(next_fast & (1u << i)));
            wire::Message(notification, 3, body);
            QueueStatus("cormoran_rip", notification);
        }
    }
    previous = current;
    fast = next_fast;
    pointer_inertia = current;
}
wire::PeripheralPending Pending(unsigned source, const std::string& subsystem, uint32_t id) {
    wire::PeripheralPending p;
    p.source = source;
    p.subsystem = subsystem;
    p.id = id;
    p.transport = active_transport;
    p.epoch = sessions[active_transport].epoch;
    p.deadline = uint32_t(esp_timer_get_time() / 1000) + 3000;
    return p;
}
wire::Bytes PeripheralRequest(const std::string& subsystem, const wire::Bytes& payload,
                              bool unlocked) {
    wire::Reader request(payload);
    const auto* op = request.Operation();
    if (!request.valid || !op || op->wire != 2)
        return wire::CustomError("Malformed peripheral request");
    wire::Reader args(op->data, op->size);
    if (!args.valid || relay_pending.size() >= 8)
        return wire::CustomError("Peripheral request queue full or malformed");
    if (subsystem == "cormoran__kscan_diagnostics") {
        const auto* data = args.Get(2);
        if (op->tag != 8 || !args.Numbers({1}) || args.Value(1) > 255 || !data || data->wire != 2 ||
            !data->size || data->size > 32)
            return wire::CustomError("Invalid peripheral diagnostics query");
        wire::Reader inner(data->data, data->size);
        const auto* action = inner.Operation();
        if (!inner.valid || !action || action->wire != 2 || action->tag > 7 ||
            (action->tag == 7 && !unlocked))
            return wire::CustomError("Invalid or locked diagnostics operation");
        const uint32_t id = args.Value(1);
        for (const auto& pending : relay_pending)
            if (pending.subsystem == subsystem && pending.id == id)
                return wire::CustomError("Diagnostics request id is already pending");
        const auto carrier =
            wire::DiagnosticsCarrier(id, wire::Bytes(data->data, data->data + data->size));
        bool sent = false;
        for (unsigned source = 1; source <= 2; ++source)
            if (CubeSplitQueueRelay(source, "KDq", carrier)) {
                relay_pending.push_back(Pending(source, subsystem, id));
                sent = true;
            }
        if (!sent)
            return wire::CustomError("No connected peripheral relay");
        wire::Bytes response;
        wire::Message(response, 8, {});
        return response;
    }
    const unsigned source_field = op->tag == 1 ? 1 : op->tag == 2 ? 2 : 3;
    const auto source = args.Value(source_field);
    if (!source || source > 2 || op->tag > 3 || (op->tag == 3 && !unlocked))
        return wire::CustomError("Invalid or locked peripheral watchdog operation");
    if (!relay_id)
        return wire::CustomError("Relay request id exhausted; restart Cube");
    const uint32_t id = relay_id++;
    const auto carrier = wire::WatchdogCarrier(id, payload);
    if (carrier.empty() || !CubeSplitQueueRelay(source, "wdq", carrier))
        return wire::CustomError("Peripheral relay unavailable");
    relay_pending.push_back(Pending(source, subsystem, id));
    wire::Bytes deferred, response;
    wire::Number(deferred, 1, id);
    wire::Message(response, 5, deferred);
    return response;
}
struct ActivityReply {
    uint32_t idle = 0, sleep = 0;
    uint8_t request = 0;
    bool received = false;
};
std::array<ActivityReply, 2> activity_replies{};
void ProcessRelayEvents() {
    std::deque<RelayEvent> events;
    {
        std::lock_guard<std::mutex> lock(relay_incoming_mutex);
        events.swap(relay_incoming);
    }
    for (const auto& event : events) {
        if (event.name == "srp" && event.data.size() == 12) {
            const auto word = [&](size_t at) {
                return uint32_t(event.data[at]) | (uint32_t(event.data[at + 1]) << 8) |
                       (uint32_t(event.data[at + 2]) << 16) | (uint32_t(event.data[at + 3]) << 24);
            };
            activity_replies[event.source - 1] = {word(0), word(4), event.data[9], true};
        }
        for (auto it = relay_pending.begin(); it != relay_pending.end();) {
            auto& pending = *it;
            if (pending.source != event.source) {
                ++it;
                continue;
            }
            wire::Bytes payload, response;
            bool done = false;
            if (event.name == "KDr" && pending.subsystem == "cormoran__kscan_diagnostics" &&
                event.data.size() == 195 && event.data[1] == pending.id && event.data[2] <= 192) {
                const wire::Bytes inner(event.data.begin() + 3,
                                        event.data.begin() + 3 + event.data[2]);
                wire::Reader checked(inner);
                if (!checked.valid || !checked.Operation()) {
                    ++it;
                    continue;
                }
                wire::Number(payload, 1, event.source);
                wire::Number(payload, 2, pending.id);
                wire::Message(payload, 3, inner);
                done = true;
            } else if (event.name == "wdp" && pending.subsystem == "cormoran__watchdog" &&
                       wire::WatchdogReply(event.data, pending, response)) {
                wire::Number(payload, 1, event.source);
                wire::Number(payload, 2, pending.id);
                wire::Message(payload, 3, response);
                wire::Bytes wrapped;
                wire::Message(wrapped, 2, payload);
                payload = std::move(wrapped);
                done = true;
            } else if (event.name == "srp" && pending.subsystem == "zmk__settings" &&
                       event.data.size() == 12 && event.data[9] == pending.id) {
                wire::Bytes settings, report;
                const auto& current = activity_replies[event.source - 1];
                wire::Number(settings, 1, current.idle);
                wire::Number(settings, 2, current.sleep);
                wire::Number(settings, 3, event.source);
                wire::Message(report, 1, settings);
                wire::Message(payload, 1, report);
                done = true;
            }
            if (done) {
                RelayNotify(pending, payload);
                it = relay_pending.erase(it);
            } else
                ++it;
        }
    }
}
bool RelaySettings(unsigned source, uint32_t idle, uint32_t sleep, bool change) {
    if (!source || source > 2 || relay_pending.size() >= 8)
        return false;
    const uint8_t id = uint8_t(relay_id++);
    if (change) {
        wire::Bytes carrier(12, 0);
        for (unsigned i = 0; i < 4; ++i) {
            carrier[i] = idle >> (8 * i);
            carrier[4 + i] = sleep >> (8 * i);
        }
        carrier[8] = 255;
        if (!CubeSplitQueueRelay(source, "as", carrier))
            return false;
    }
    activity_replies[source - 1].received = false;
    if (!CubeSplitQueueRelay(source, "srq", {id}))
        return false;
    relay_pending.push_back(Pending(source, "zmk__settings", id));
    if (!change)
        return true;
    const int64_t deadline = esp_timer_get_time() + 1500000;
    while (esp_timer_get_time() < deadline) {
        ProcessRelayEvents();
        const auto& reply = activity_replies[source - 1];
        if (reply.received && reply.request == id)
            return reply.idle == idle && reply.sleep == sleep;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return false;
}
void UnsavedNotification() {
    wire::Bytes body, notification, message;
    wire::Number(body, 1, rpc.Dirty());
    wire::Message(notification, 5, body);
    wire::Message(message, 2, notification);
    for (size_t i = 0; i < sessions.size(); ++i)
        if (sessions[i].connected && sessions[i].used)
            Send(i, message);
}
bool Feed(size_t transport, uint8_t byte, uint32_t epoch) {
    auto& session = sessions[transport];
    if (!session.connected || epoch != session.epoch ||
        (transport ? (!CubeBleStudioReady() || epoch != CubeBleStudioEpoch())
                   : (!CubeUsbAttached() || epoch != CubeUsbEpoch())))
        return transport != 0;
    session.last_byte = esp_timer_get_time();
    return session.framer.Feed(byte, [transport](const wire::Bytes& payload) {
        auto& session = sessions[transport];
        session.used = true;
        session.tx_failed = false;
        if (watchdog_subscribed)
            esp_task_wdt_reset();
        active_transport = transport;
        rpc.LockState(session.unlocked && esp_timer_get_time() < session.until);
        const bool before_dirty = rpc.Dirty(), before_unlock = rpc.Unlocked();
        pending_notifications.clear();
        auto response = rpc.Handle(payload, Save);
        session.unlocked = rpc.Unlocked();
        if (!session.unlocked)
            session.until = 0;
        UpdateCache();
        Send(transport, response);
        for (const auto& notification : pending_notifications)
            Send(transport, notification);
        pending_notifications.clear();
        if (before_dirty != rpc.Dirty())
            UnsavedNotification();
        if (before_unlock != session.unlocked)
            Send(transport, rpc.LockState(session.unlocked));
        PublishStreamSessions();
    });
}
}  // namespace
void CubeStudioInitialize() {
    std::call_once(initialize_once, [] {
        nvs_handle_t handle;
        if (nvs_open("cube_studio", NVS_READONLY, &handle) == ESP_OK) {
            size_t size = 0;
            if (nvs_get_blob(handle, "keymap_v1", nullptr, &size) == ESP_OK && size > 0 &&
                size < 2048) {
                wire::Bytes record(size);
                if (nvs_get_blob(handle, "keymap_v1", record.data(), &size) == ESP_OK)
                    rpc.Load(record);
            }
            size = 0;
            if (nvs_get_blob(handle, "macros_v1", nullptr, &size) == ESP_OK && size > 0 &&
                size <= 3072) {
                wire::Bytes record(size);
                if (nvs_get_blob(handle, "macros_v1", record.data(), &size) == ESP_OK)
                    macros.Load(record);
            }
            uint32_t activity[2]{};
            size = 25;
            wire::Bytes default_record(size);
            if (nvs_get_blob(handle, "defaults_v1", default_record.data(), &size) == ESP_OK &&
                size == 25)
                defaults.Load(default_record);
            default_cache = defaults;
            size = 488;
            wire::Bytes pointer_record(size);
            if (nvs_get_blob(handle, "pointers_v1", pointer_record.data(), &size) == ESP_OK &&
                size == 488)
                pointers.Load(pointer_record);
            pointer_cache = pointers.live;
            size = 200;
            wire::Bytes sensor_record(size);
            if (nvs_get_blob(handle, "sensors_v1", sensor_record.data(), &size) == ESP_OK &&
                size == 200)
                sensors.Load(sensor_record);
            sensor_cache = sensors;
            size = 0;
            if (nvs_get_blob(handle, "custom_v1", nullptr, &size) == ESP_OK && size >= 8 &&
                size <= 4096) {
                wire::Bytes custom_record(size);
                if (nvs_get_blob(handle, "custom_v1", custom_record.data(), &size) == ESP_OK)
                    custom_settings.Load(custom_record);
            }
            tapping_term = custom_settings.Term();
            size = sizeof(activity);
            if (nvs_get_blob(handle, "activity_v1", activity, &size) == ESP_OK &&
                size == sizeof(activity) && activity[0] <= 86400000 && activity[1] <= 86400000) {
                activity_idle = activity[0];
                activity_sleep = activity[1];
            }
            size = 0;
            if (nvs_get_blob(handle, "watchdog_v1", nullptr, &size) == ESP_OK && size > 0 &&
                size <= 256) {
                wire::Bytes record(size);
                if (nvs_get_blob(handle, "watchdog_v1", record.data(), &size) == ESP_OK)
                    watchdog_log.Load(record);
            }
            size = 0;
            if (nvs_get_blob(handle, "combos_v1", nullptr, &size) == ESP_OK && size > 0 &&
                size <= 1024) {
                wire::Bytes record(size);
                if (nvs_get_blob(handle, "combos_v1", record.data(), &size) == ESP_OK)
                    combos.Load(record);
            }
            nvs_close(handle);
        }
        uint8_t mac[6];
        if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
            char serial[13];
            for (size_t i = 0; i < 6; ++i)
                std::snprintf(serial + i * 2, 3, "%02X", mac[i]);
            rpc.serial = serial;
        }
        UpdateCache();
        macro_cache = macros.model;
        combo_cache = combos.model;
        rpc.custom.push_back(
            {"cormoran__runtime_combo", "https://cormoran.github.io/zmk-feature-runtime-combo/",
             [](const wire::Bytes& payload, bool unlocked) {
                 const auto before = combos.model.Export();
                 auto response = combos.Handle(payload, unlocked, [](const wire::Bytes& bytes) {
                     return SaveRecord("combos_v1", bytes);
                 });
                 if (before != combos.model.Export()) {
                     std::lock_guard<std::mutex> lock(combo_mutex);
                     combo_cache = combos.model;
                     ++combo_revision;
                 }
                 return response;
             }});
        activity_last = esp_timer_get_time();
        watchdog_log.Boot(ResetBits());
        if (!SaveRecord("watchdog_v1", watchdog_log.Export()))
            ESP_LOGW("CubeStudio", "Boot incident is only in RAM: NVS commit failed");
        rpc.custom.push_back(
            {"cormoran__watchdog", "https://cormoran.github.io/zmk-feature-watchdog/",
             [](const wire::Bytes& payload, bool unlocked) {
                 wire::Reader request(payload);
                 if (const auto* op = request.Operation()) {
                     wire::Reader args(op->data, op->size);
                     if (args.Value(op->tag == 1 ? 1 : op->tag == 2 ? 2 : 3))
                         return PeripheralRequest("cormoran__watchdog", payload, unlocked);
                 }
                 return watchdog_log.Handle(payload, unlocked, [](const wire::Bytes& bytes) {
                     return SaveRecord("watchdog_v1", bytes);
                 });
             }});
        rpc.custom.push_back({"zmk__settings",
                              "https://cormoran.github.io/zmk-module-settings-rpc/",
                              ActivitySettings});
        rpc.custom.push_back({"cormoran__runtime_macro",
                              "https://cormoran.github.io/zmk-feature-runtime-macro/",
                              [](const wire::Bytes& payload, bool unlocked) {
                                  auto response = macros.Handle(payload, unlocked, SaveMacros);
                                  std::lock_guard<std::mutex> lock(macro_mutex);
                                  macro_cache = macros.model;
                                  return response;
                              }});
        rpc.custom.push_back({"zmk__device_info",
                              "https://cormoran.github.io/zmk-feature-device-info/", DeviceInfo});
        rpc.custom.push_back({"cormoran_ble",
                              "https://cormoran.github.io/zmk-module-ble-management/",
                              CubeBleManagement});
        rpc.custom.push_back(
            {"cormoran__fast_keymap", "https://cormoran.github.io/zmk-feature-fast-keymap/",
             [](const wire::Bytes& payload, bool) { return wire::FastKeymap(payload, rpc); }});
        rpc.custom.push_back(
            {"cormoran__default_layer", "https://cormoran.github.io/zmk-feature-default-layer/",
             [](const wire::Bytes& payload, bool unlocked) {
                 defaults.detected[0] = CubeUsbDetectedOs();
                 auto response = defaults.Handle(
                     payload, unlocked, rpc.model, ActiveEndpoint(),
                     [](const wire::Bytes& bytes) { return SaveRecord("defaults_v1", bytes); });
                 std::lock_guard<std::mutex> lock(cache_mutex);
                 default_cache = defaults;
                 return response;
             }});
        rpc.custom.push_back(
            {"cormoran__os_detection", "https://cormoran.github.io/zmk-feature-os-detection/",
             [](const wire::Bytes& payload, bool unlocked) {
                 defaults.detected[0] = CubeUsbDetectedOs();
                 auto response = defaults.Os(
                     payload, unlocked, ActiveEndpoint(), CubeDualBoundProfiles(),
                     CubeUsbAttached(), CubeDualBleReady(),
                     [](const wire::Bytes& bytes) { return SaveRecord("defaults_v1", bytes); });
                 std::lock_guard<std::mutex> lock(cache_mutex);
                 default_cache = defaults;
                 return response;
             }});
        rpc.custom.push_back(
            {"zmk__input_stream", "https://cormoran.github.io/zmk-feature-input-stream/",
             [](const wire::Bytes& payload, bool unlocked) {
                 wire::Reader request(payload);
                 const auto* op = request.Operation();
                 if (!request.valid || !op || op->wire != 2 || (op->tag != 1 && op->tag != 2))
                     return wire::CustomError("Invalid input stream request");
                 if (op->tag == 1 && !unlocked)
                     return wire::CustomError("Studio is locked");
                 sessions[active_transport].stream = op->tag == 1;
                 wire::Bytes response;
                 wire::Message(response, 2, {});
                 return response;
             }});
        rpc.custom.push_back(
            {"cormoran_rip", "https://cormoran.github.io/zmk-module-runtime-input-processor/",
             [](const wire::Bytes& payload, bool unlocked) {
                 auto response = pointers.Handle(
                     payload, unlocked, pointer_hardware.load(), rpc.model,
                     [](const wire::Bytes& bytes) { return SaveRecord("pointers_v1", bytes); },
                     [](const wire::Bytes& notification) {
                         if (pending_notifications.size() < 8)
                             pending_notifications.push_back(
                                 CustomNotification("cormoran_rip", notification));
                     },
                     pointer_inertia.load());
                 std::lock_guard<std::mutex> lock(pointer_mutex);
                 pointer_cache = pointers.live;
                 return response;
             }});
        rpc.custom.push_back(
            {"cormoran_rsr", "https://cormoran.github.io/zmk-behavior-runtime-sensor-rotate/",
             [](const wire::Bytes& payload, bool unlocked) {
                 auto response = sensors.Handle(
                     payload, unlocked, sensor_hardware.load(), rpc.model,
                     [](const wire::Bytes& bytes) { return SaveRecord("sensors_v1", bytes); });
                 std::lock_guard<std::mutex> lock(sensor_mutex);
                 sensor_cache = sensors;
                 return response;
             }});
        rpc.custom.push_back(
            {"cormoran__physical_layouts", "", [](const wire::Bytes& payload, bool) {
                 wire::Reader request(payload);
                 const auto* op = request.Operation();
                 if (!request.valid || !op || op->tag != 1 || op->wire != 2)
                     return wire::CustomError("Invalid physical layout request");
                 wire::Bytes body, response;
                 for (unsigned i = 0; i < 4; ++i)
                     if (pointer_hardware.load() & (1u << i)) {
                         wire::Bytes device, link;
                         wire::Text(device, 1, std::to_string(i));
                         wire::Text(device, 2, "Split Pointer " + std::to_string(i + 1));
                         wire::Text(link, 1, std::to_string(i));
                         wire::Text(link, 2, "cormoran_rip");
                         wire::Message(device, 3, link);
                         wire::Message(device, 4, {});
                         wire::Number(device, 8, 1);
                         wire::Message(body, 1, device);
                     }
                 for (unsigned i = 0; i < 4; ++i)
                     if (sensor_hardware.load() & (1u << i)) {
                         wire::Bytes encoder;
                         wire::Number(encoder, 1, 1);
                         wire::Message(body, 2, encoder);
                     }
                 wire::Message(response, 2, body);
                 return response;
             }});
        rpc.custom.push_back(
            {"cormoran_custom_settings", "https://cormoran.github.io/zmk-feature-custom-settings/",
             [](const wire::Bytes& payload, bool unlocked) {
                 custom_settings.Session(active_transport, sessions[active_transport].epoch);
                 size_t index = 0;
                 while (index < rpc.custom.size() &&
                        rpc.custom[index].identifier != "cormoran_custom_settings")
                     ++index;
                 auto response = custom_settings.Handle(
                     payload, unlocked, index,
                     [](const wire::Bytes& bytes) { return SaveRecord("custom_v1", bytes); },
                     [](const wire::Bytes& notification) {
                         if (pending_notifications.size() < 32)
                             pending_notifications.push_back(
                                 CustomNotification("cormoran_custom_settings", notification));
                     });
                 tapping_term = custom_settings.Term();
                 return response;
             }});
        rpc.custom.push_back({"cormoran__kscan_diagnostics",
                              "https://cormoran.github.io/zmk-feature-kscan-diagnostics/",
                              [](const wire::Bytes& payload, bool unlocked) {
                                  wire::Reader request(payload);
                                  if (request.Operation() && request.Operation()->tag == 8)
                                      return PeripheralRequest("cormoran__kscan_diagnostics",
                                                               payload, unlocked);
                                  std::lock_guard<std::mutex> lock(diagnostics_mutex);
                                  return diagnostics.Handle(payload, unlocked,
                                                            uint32_t(esp_timer_get_time() / 1000));
                              }});
        initialized_for_notifications = true;
    });
}
void CubeStudioRecordPositions(const cube_dongle::SplitState::Snapshot& positions) {
    static cube_dongle::SplitState::Snapshot last{};
    if (positions != last) {
        activity_last = esp_timer_get_time();
        last = positions;
    }
    std::lock_guard<std::mutex> lock(diagnostics_mutex);
    diagnostics.Sample(positions, uint32_t(esp_timer_get_time() / 1000));
}
void CubeStudioMacroTriggers(const cube_dongle::KeyboardState<61, 4>& keyboard) {
    for (size_t p = 0; p < macro_keys.size(); ++p) {
        const auto binding = keyboard.PressedBinding(p);
        const bool pressed = binding.kind == cube_dongle::Kind::RuntimeMacro;
        if (pressed && !macro_keys[p] && binding.value < 8) {
            std::lock_guard<std::mutex> lock(macro_mutex);
            macro_player.Start(macro_cache.entries[binding.value], macro_cache.tap_ms,
                               uint32_t(esp_timer_get_time() / 1000));
        }
        macro_keys[p] = pressed;
    }
}
cube_dongle::ReportFrame CubeStudioMacroOutput(cube_dongle::ReportFrame physical) {
    macro_player.Tick(uint32_t(esp_timer_get_time() / 1000));
    if (macro_player.Active())
        activity_last = esp_timer_get_time();
    return wire::MergeReports(macro_player.Merge(physical), sensor_output.report);
}
void CubeStudioCancelMacros() {
    CubeSplitCancelHoldTaps();
    macro_player.Cancel();
    pointer_engine.CancelMotion();
    sensor_engine.Cancel();
    sensor_output = {};
    if (!combo_engine.Neutral())
        combo_engine.Cancel();
}
uint8_t CubeStudioCurrentModifiers(uint8_t physical_modifiers) {
    cube_dongle::ReportFrame physical;
    physical.keyboard[0] = physical_modifiers;
    return wire::MergeReports(macro_player.Merge(physical), sensor_output.report).keyboard[0];
}
wire::ComboResult CubeStudioFilterPositions(const cube_dongle::SplitState::Snapshot& raw,
                                            size_t layer) {
    if (combo_engine.Neutral()) {
        std::lock_guard<std::mutex> lock(combo_mutex);
        if (combo_revision != combo_seen && combo_engine.Configure(combo_cache))
            combo_seen = combo_revision;
    }
    auto result = combo_engine.Apply(raw, uint32_t(esp_timer_get_time() / 1000), layer);
    if (result.macro_mask) {
        std::lock_guard<std::mutex> lock(macro_mutex);
        for (size_t slot = 0; slot < 8; ++slot)
            if (result.macro_mask & (1u << slot))
                macro_player.Start(macro_cache.entries[slot], macro_cache.tap_ms,
                                   uint32_t(esp_timer_get_time() / 1000));
    }
    result.layers |= pointer_engine.Layers(uint32_t(esp_timer_get_time() / 1000));
    result.layers |= sensor_output.layers;
    return result;
}
bool CubeStudioCombosNeedTick() {
    return combo_engine.NeedsTick() || pointer_engine.NeedsTick() || sensor_engine.NeedsTick();
}
void CubeStudioSetActivityHandler(std::function<void(unsigned)> handler) {
    std::lock_guard<std::mutex> lock(activity_mutex);
    activity_handler = std::move(handler);
}
void CubeStudioActivityWake() { activity_last = esp_timer_get_time(); }
void CubeStudioPoll() {
    ProcessRelayEvents();
    if (!watchdog_subscribed) {
        auto status = esp_task_wdt_status(nullptr);
        if (status == ESP_ERR_INVALID_STATE) {
            const esp_task_wdt_config_t config = {10000, 0, true};
            if (esp_task_wdt_init(&config) == ESP_OK)
                status = esp_task_wdt_status(nullptr);
        }
        watchdog_subscribed = status == ESP_OK ||
                              (status == ESP_ERR_NOT_FOUND && esp_task_wdt_add(nullptr) == ESP_OK);
    }
    if (watchdog_subscribed)
        esp_task_wdt_reset();
    const int64_t now = esp_timer_get_time();
    const uint32_t elapsed = uint32_t((now - activity_last.load()) / 1000);
    const auto idle = activity_idle.load(), sleep = activity_sleep.load();
    const unsigned state = sleep && elapsed >= sleep ? 2 : idle && elapsed >= idle ? 1 : 0;
    if (state != activity_state) {
        activity_state = state;
        Application::GetInstance().Schedule([state] {
            auto& app = Application::GetInstance();
            const uint32_t elapsed = uint32_t((esp_timer_get_time() - activity_last.load()) / 1000);
            const auto idle = activity_idle.load(), sleep = activity_sleep.load();
            const unsigned current = sleep && elapsed >= sleep ? 2
                                     : idle && elapsed >= idle ? 1
                                                               : 0;
            if (state != current)
                return;
            if (state && (!app.CanEnterSleepMode() || (CubeSplitBenchStatus() >> 8))) {
                CubeStudioActivityWake();
                return;
            }
            std::lock_guard<std::mutex> lock(activity_mutex);
            if (activity_handler)
                activity_handler(state);
        });
    }
    const bool ready[] = {CubeUsbAttached(), CubeBleStudioReady()};
    const uint32_t epochs[] = {CubeUsbEpoch(), CubeBleStudioEpoch()};
    const bool unlock = unlock_request.exchange(false);
    for (size_t i = 0; i < sessions.size(); ++i) {
        auto& session = sessions[i];
        if (!ready[i] || session.epoch != epochs[i]) {
            session = Session{};
            session.epoch = epochs[i];
        }
        if (!ready[i])
            continue;
        session.connected = true;
        if (session.framer.Active() && now - session.last_byte > 2000000)
            session.framer.Reset();
        if (unlock) {
            session.unlocked = true;
            session.until = now + 300000000;
            if (session.used)
                Send(i, rpc.LockState(true));
        }
        if (session.unlocked && now >= session.until) {
            session.unlocked = false;
            if (session.used)
                Send(i, rpc.LockState(false));
        }
    }
    if (stream_overflow.exchange(false)) {
        for (auto& session : sessions)
            session.stream = false;
        ESP_LOGW("CubeStudio",
                 "Input stream queue overflow; streaming disabled, reconnect and re-enable");
    }
    PublishStreamSessions();
    std::deque<TargetNotification> stream;
    {
        std::lock_guard<std::mutex> lock(stream_mutex);
        stream.swap(stream_notifications);
    }
    std::deque<TargetNotification> status;
    {
        std::lock_guard<std::mutex> lock(stream_mutex);
        status.swap(status_notifications);
    }
    for (const auto& message : status) {
        const auto& session = sessions[message.transport];
        if (session.connected && session.used && session.epoch == message.epoch)
            Send(message.transport, message.body);
    }
    for (const auto& message : stream) {
        const auto& session = sessions[message.transport];
        if ((stream_mask.load() & (1u << message.transport)) && session.epoch == message.epoch)
            Send(message.transport, CustomNotification("zmk__input_stream", message.body));
    }
    while (!relay_notifications.empty()) {
        auto message = std::move(relay_notifications.front());
        relay_notifications.pop_front();
        const auto& session = sessions[message.transport];
        if (session.connected && session.used && session.epoch == message.epoch)
            Send(message.transport, message.body);
    }
    const uint32_t milliseconds = uint32_t(now / 1000);
    relay_pending.erase(std::remove_if(relay_pending.begin(), relay_pending.end(),
                                       [&](const wire::PeripheralPending& pending) {
                                           return pending.Expired(milliseconds) ||
                                                  !sessions[pending.transport].connected ||
                                                  sessions[pending.transport].epoch !=
                                                      pending.epoch;
                                       }),
                        relay_pending.end());
}
void CubeStudioRelayEvent(unsigned source, const std::string& name,
                          const std::vector<uint8_t>& data) {
    if (!source || source > 2 || data.size() > 255 ||
        (name != "KDr" && name != "wdp" && name != "srp"))
        return;
    std::lock_guard<std::mutex> lock(relay_incoming_mutex);
    if (relay_incoming.size() < 16)
        relay_incoming.push_back({source, name, data});
}
void CubeStudioStreamPositions(const cube_dongle::SplitState::Snapshot& raw,
                               const cube_dongle::KeyboardState<61, 4>& keyboard) {
    static cube_dongle::SplitState::Snapshot previous{};
    static std::array<cube_dongle::Binding, 61> bindings{};
    static size_t layer = 0;
    const uint8_t mask = stream_mask.load();
    auto enqueue = [&](const wire::Bytes& notification) {
        if (!mask)
            return;
        std::lock_guard<std::mutex> lock(stream_mutex);
        for (unsigned transport = 0; transport < 2; ++transport)
            if (mask & (1u << transport)) {
                if (stream_notifications.size() >= 128) {
                    stream_overflow = true;
                    stream_mask = 0;
                    return;
                }
                stream_notifications.push_back(
                    {transport, stream_epochs[transport].load(), notification});
            }
    };
    for (size_t p = 0; p < 61; ++p) {
        const bool down = raw[p / 8] & (1u << (p % 8));
        if (down == bool(previous[p / 8] & (1u << (p % 8))))
            continue;
        if (down)
            bindings[p] = keyboard.BindingAt(p);
        const auto& binding = bindings[p];
        wire::Bytes event, notification;
        wire::Number(event, 1, p);
        wire::Number(event, 2, down);
        wire::Number(event, 3, wire::Behavior(binding));
        wire::Number(event, 4, wire::Param1(binding));
        wire::Number(event, 5, wire::Param2(binding));
        wire::Message(notification, 1, event);
        enqueue(notification);
    }
    previous = raw;
    if (layer != keyboard.ActiveLayer()) {
        layer = keyboard.ActiveLayer();
        wire::Bytes event, notification;
        wire::Number(event, 1, layer);
        wire::Message(notification, 2, event);
        enqueue(notification);
    }
}
bool CubeStudioFeed(uint8_t byte) { return Feed(0, byte, CubeUsbEpoch()); }
bool CubeStudioFeedBle(uint8_t byte, uint32_t epoch) { return Feed(1, byte, epoch); }
bool CubeStudioUnlockUsb() {
    if (!CubeUsbAttached() && !CubeBleStudioReady())
        return false;
    unlock_request = true;
    return true;
}
void CubeStudioRefreshKeymap(cube_dongle::KeyboardState<61, 4>& keyboard) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    keyboard.SetKeymap(live_map);
    keyboard.SetLayerOrder(live_order, live_count);
    keyboard.SetTappingTerm(tapping_term.load());
    const unsigned endpoint = ActiveEndpoint();
    default_cache.detected[0] = CubeUsbDetectedOs();
    const int requested = default_cache.endpoints[endpoint];
    const int layer =
        requested == -2 ? default_cache.os_layers[default_cache.EffectiveOs(endpoint)] : requested;
    const bool present =
        layer >= 0 && std::find(live_order.begin(), live_order.begin() + live_count,
                                size_t(layer)) != live_order.begin() + live_count;
    keyboard.SetDefaultLayer(present ? size_t(layer) : 4);
}
void CubeStudioPointerHardware(unsigned index, bool present) {
    if (index >= 4)
        return;
    pointer_engine.Register(index, present);
    if (present)
        pointer_hardware.fetch_or(1u << index);
    else
        pointer_hardware.fetch_and(~(1u << index));
    cube_dongle::PointerReport report;
    report.buttons = pointer_engine.Buttons();
    CubeDualPointer(report);
}
void CubeStudioSensorHardware(unsigned index, uint16_t triggers) {
    if (index >= 4)
        return;
    if (triggers > 360)
        triggers = 0;
    sensor_engine.Register(index, triggers);
    sensor_output = {};
    if (triggers)
        sensor_hardware.fetch_or(1u << index);
    else
        sensor_hardware.fetch_and(~(1u << index));
}
void CubeStudioSensorInput(unsigned source, const uint8_t* bytes, size_t length, size_t layer) {
    if (!source || source > 2 || !bytes || length < 14 || (length - 2) % 12 || bytes[0] >= 2 ||
        !bytes[1] || bytes[1] > 4 || size_t(bytes[1]) * 12 + 2 > length)
        return;
    const unsigned index = (source - 1) * 2 + bytes[0];
    if (!(sensor_hardware.load() & (1u << index)))
        return;
    const auto word = [&](size_t at) {
        return uint32_t(bytes[at]) | (uint32_t(bytes[at + 1]) << 8) |
               (uint32_t(bytes[at + 2]) << 16) | (uint32_t(bytes[at + 3]) << 24);
    };
    std::lock_guard<std::mutex> lock(sensor_mutex);
    if (sensor_engine.Input(index, int32_t(word(2)), int32_t(word(6)), layer, sensor_cache))
        CubeStudioActivityWake();
}
bool CubeStudioSensorTick() {
    const auto previous = sensor_output;
    sensor_output = sensor_engine.Tick(uint32_t(esp_timer_get_time() / 1000));
    if (sensor_output.actions)
        CubeDualActions(sensor_output.actions);
    for (unsigned slot = 0; slot < 8; ++slot)
        if (sensor_output.macro_mask & (1u << slot)) {
            std::lock_guard<std::mutex> lock(macro_mutex);
            macro_player.Start(macro_cache.entries[slot], macro_cache.tap_ms,
                               uint32_t(esp_timer_get_time() / 1000));
        }
    return !(previous.report == sensor_output.report) || previous.layers != sensor_output.layers;
}
void CubeStudioPointerInput(unsigned index, const uint8_t* bytes, size_t length, size_t layer) {
    {
        std::lock_guard<std::mutex> lock(pointer_mutex);
        pointer_engine.Configure(pointer_cache);
    }
    cube_dongle::PointerReport report;
    if (pointer_engine.Input(index, bytes, length, uint32_t(esp_timer_get_time() / 1000), layer,
                             report)) {
        CubeStudioActivityWake();
        CubeDualPointer(report);
    }
    PointerNotifyStatus();
}
void CubeStudioPointerTick(size_t layer) {
    {
        std::lock_guard<std::mutex> lock(pointer_mutex);
        pointer_engine.Configure(pointer_cache);
    }
    for (const auto& report : pointer_engine.Tick(uint32_t(esp_timer_get_time() / 1000), layer))
        CubeDualPointer(report);
    PointerNotifyStatus();
}
void CubeStudioLayerName(size_t layer, char* name, size_t capacity) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    if (name && capacity)
        std::snprintf(name, capacity, "%s", layer < 4 ? live_names[layer].c_str() : "?");
}

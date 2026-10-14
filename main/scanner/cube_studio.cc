#include "cube_studio.h"
#include <esp_app_desc.h>
#include <esp_flash.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <nvs.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include "../../experimental/cube_dongle/runtime_macros.h"
#include "../../experimental/cube_dongle/studio_diagnostics.h"
#include "../../experimental/cube_dongle/studio_protocol.h"
#include "../../experimental/cube_dongle/studio_watchdog.h"
#include "application.h"
#include "cube_ble_dongle.h"
#include "cube_usb_dongle.h"

namespace {
namespace wire = cube_dongle::studio;
wire::Rpc rpc;
wire::RuntimeMacros macros;
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
        if (!settings.valid || !settings.Numbers({1, 2, 3}) || settings.Value(3) ||
            idle > 86400000 || sleep > 86400000 || (idle && idle < 1000) ||
            (sleep && sleep < 1000) || (idle && sleep && sleep < idle))
            return wire::CustomError("Invalid timeout or unavailable peripheral source");
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
        wire::Number(body, 1, 1);
    } else
        return wire::CustomError(
            "Peripheral activity relay not implemented; read central settings");
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
    bool connected = false, unlocked = false, used = false;
    int64_t until = 0, last_byte = 0;
    uint32_t epoch = 0;
};
std::array<Session, 2> sessions;
std::once_flag initialize_once;
std::mutex cache_mutex;
cube_dongle::KeyboardState<61, 3>::Keymap live_map = cube_dongle::VelvetKeymap();
std::array<std::string, 3> live_names{{"BASE", "NVA", "SYS"}};
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
}
void Send(size_t transport, const wire::Bytes& payload) {
    const auto bytes = wire::Frame(payload);
    const bool sent =
        transport == 0 ? CubeCdcWritePacket(bytes.data(), bytes.size())
                       : CubeBleStudioWrite(bytes.data(), bytes.size(), sessions[transport].epoch);
    if (!sent)
        ESP_LOGW("CubeStudio", "Response interrupted; reconnect Studio");
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
    if (!session.connected || (transport && (epoch != session.epoch || !CubeBleStudioReady() ||
                                             epoch != CubeBleStudioEpoch())))
        return transport != 0;
    session.last_byte = esp_timer_get_time();
    return session.framer.Feed(byte, [transport](const wire::Bytes& payload) {
        auto& session = sessions[transport];
        session.used = true;
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
        activity_last = esp_timer_get_time();
        watchdog_log.Boot(ResetBits());
        if (!SaveRecord("watchdog_v1", watchdog_log.Export()))
            ESP_LOGW("CubeStudio", "Boot incident is only in RAM: NVS commit failed");
        rpc.custom.push_back(
            {"cormoran__watchdog", "https://cormoran.github.io/zmk-feature-watchdog/",
             [](const wire::Bytes& payload, bool unlocked) {
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
        rpc.custom.push_back({"cormoran__kscan_diagnostics",
                              "https://cormoran.github.io/zmk-feature-kscan-diagnostics/",
                              [](const wire::Bytes& payload, bool unlocked) {
                                  std::lock_guard<std::mutex> lock(diagnostics_mutex);
                                  return diagnostics.Handle(payload, unlocked,
                                                            uint32_t(esp_timer_get_time() / 1000));
                              }});
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
void CubeStudioMacroTriggers(const cube_dongle::KeyboardState<61, 3>& keyboard) {
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
    return macro_player.Merge(physical);
}
void CubeStudioCancelMacros() { macro_player.Cancel(); }
void CubeStudioSetActivityHandler(std::function<void(unsigned)> handler) {
    std::lock_guard<std::mutex> lock(activity_mutex);
    activity_handler = std::move(handler);
}
void CubeStudioActivityWake() { activity_last = esp_timer_get_time(); }
void CubeStudioPoll() {
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
            if (state && !app.CanEnterSleepMode()) {
                CubeStudioActivityWake();
                return;
            }
            std::lock_guard<std::mutex> lock(activity_mutex);
            if (activity_handler)
                activity_handler(state);
        });
    }
    const bool ready[] = {CubeUsbAttached(), CubeBleStudioReady()};
    const uint32_t epoch = CubeBleStudioEpoch();
    const bool unlock = unlock_request.exchange(false);
    for (size_t i = 0; i < sessions.size(); ++i) {
        auto& session = sessions[i];
        if (!ready[i] || (i && session.epoch != epoch)) {
            session = Session{};
            session.epoch = i ? epoch : 0;
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
}
bool CubeStudioFeed(uint8_t byte) { return Feed(0, byte, 0); }
bool CubeStudioFeedBle(uint8_t byte, uint32_t epoch) { return Feed(1, byte, epoch); }
bool CubeStudioUnlockUsb() {
    if (!CubeUsbAttached() && !CubeBleStudioReady())
        return false;
    unlock_request = true;
    return true;
}
void CubeStudioRefreshKeymap(cube_dongle::KeyboardState<61, 3>& keyboard) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    keyboard.SetKeymap(live_map);
}
void CubeStudioLayerName(size_t layer, char* name, size_t capacity) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    if (name && capacity)
        std::snprintf(name, capacity, "%s", layer < 3 ? live_names[layer].c_str() : "?");
}

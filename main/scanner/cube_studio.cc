#include "cube_studio.h"
#include <esp_app_desc.h>
#include <esp_flash.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <nvs.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include "../../experimental/cube_dongle/studio_diagnostics.h"
#include "../../experimental/cube_dongle/studio_protocol.h"
#include "cube_ble_dongle.h"
#include "cube_usb_dongle.h"

namespace {
namespace wire = cube_dongle::studio;
wire::Rpc rpc;
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
        auto response = rpc.Handle(payload, Save);
        session.unlocked = rpc.Unlocked();
        if (!session.unlocked)
            session.until = 0;
        UpdateCache();
        Send(transport, response);
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
    std::lock_guard<std::mutex> lock(diagnostics_mutex);
    diagnostics.Sample(positions, uint32_t(esp_timer_get_time() / 1000));
}
void CubeStudioPoll() {
    const int64_t now = esp_timer_get_time();
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

#include "cube_studio.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <nvs.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include "../../experimental/cube_dongle/studio_protocol.h"
#include "cube_usb_dongle.h"

namespace {
namespace wire = cube_dongle::studio;
wire::Rpc rpc;
wire::Framer framer;
std::once_flag initialize_once;
std::mutex cache_mutex;
cube_dongle::KeyboardState<61, 3>::Keymap live_map = cube_dongle::VelvetKeymap();
std::array<std::string, 3> live_names{{"BASE", "NVA", "SYS"}};
std::atomic<bool> unlock_request{false};
int64_t unlocked_until = 0, last_byte = 0;
bool attached = false;
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
void Send(const wire::Bytes& payload) {
    const auto bytes = wire::Frame(payload);
    if (!bytes.empty() && !CubeCdcWritePacket(bytes.data(), bytes.size()))
        ESP_LOGW("CubeStudio", "CDC response interrupted; reconnect Studio");
}
void UnsavedNotification() {
    wire::Bytes body, notification, message;
    wire::Number(body, 1, rpc.Dirty());
    wire::Message(notification, 5, body);
    wire::Message(message, 2, notification);
    Send(message);
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
    });
}
void CubeStudioPoll() {
    const int64_t now = esp_timer_get_time();
    const bool mounted = CubeUsbAttached();
    if (!mounted) {
        unlock_request = false;
        unlocked_until = 0;
        if (rpc.Unlocked())
            rpc.LockState(false);
        framer.Reset();
        attached = false;
        return;
    }
    if (!attached) {
        framer.Reset();
        attached = true;
    }
    if (framer.Active() && now - last_byte > 2000000)
        framer.Reset();
    if (unlock_request.exchange(false)) {
        unlocked_until = now + 300000000;
        Send(rpc.LockState(true));
    }
    if (rpc.Unlocked() && now >= unlocked_until)
        Send(rpc.LockState(false));
}
bool CubeStudioFeed(uint8_t byte) {
    last_byte = esp_timer_get_time();
    return framer.Feed(byte, [](const wire::Bytes& payload) {
        const bool before_dirty = rpc.Dirty(), before_unlock = rpc.Unlocked();
        auto response = rpc.Handle(payload, Save);
        UpdateCache();
        Send(response);
        if (before_dirty != rpc.Dirty())
            UnsavedNotification();
        if (before_unlock != rpc.Unlocked())
            Send(rpc.LockState(rpc.Unlocked()));
    });
}
bool CubeStudioUnlockUsb() {
    if (!CubeUsbAttached())
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

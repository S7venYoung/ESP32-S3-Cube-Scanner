#include "zmk_scanner.h"
#include "codex_scanner_display.h"
#include "board.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_adv.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
constexpr char kTag[] = "ZmkScanner";
CodexScannerDisplay* display = nullptr;
std::vector<ScannerDevice> devices;
const std::string name_filter = CONFIG_ZMK_SCANNER_NAME_FILTER;
int64_t last_refresh_us = 0;
void BeginDiscovery();

void Refresh(bool scanning) {
    display->UpdateScanner(devices, scanning, name_filter);
    last_refresh_us = esp_timer_get_time();
}

void Prune(int64_t now) {
    devices.erase(std::remove_if(devices.begin(), devices.end(), [now](const ScannerDevice& d) {
        return now - d.last_seen_us > 10000000;
    }), devices.end());
}

int OnGapEvent(ble_gap_event* event, void*) {
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        Prune(esp_timer_get_time());
        Refresh(true);
        BeginDiscovery();
        return 0;
    }
    if (event->type != BLE_GAP_EVENT_DISC) return 0;
    ble_hs_adv_fields fields = {};
    if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0 ||
        fields.name == nullptr || fields.name_len == 0) return 0;
    const std::string name(reinterpret_cast<const char*>(fields.name), fields.name_len);
    if (!name_filter.empty() && name.find(name_filter) == std::string::npos) return 0;
    const int64_t now = esp_timer_get_time();
    Prune(now);
    const auto* addr = event->disc.addr.val;
    char address[18];
    std::snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X",
                  addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    auto existing = std::find_if(devices.begin(), devices.end(), [&address](const ScannerDevice& d) {
        return d.address == address;
    });
    ScannerDevice latest{name, address, event->disc.rssi, now};
    if (existing != devices.end()) *existing = latest;
    else if (devices.size() < 2) devices.push_back(latest);
    else {
        auto weakest = std::min_element(devices.begin(), devices.end(), [](const ScannerDevice& a, const ScannerDevice& b) {
            return a.rssi < b.rssi;
        });
        if (latest.rssi > weakest->rssi) *weakest = latest;
    }
    if (now - last_refresh_us >= 250000) Refresh(true);
    return 0;
}

void BeginDiscovery() {
    uint8_t address_type;
    int rc = ble_hs_id_infer_auto(0, &address_type);
    if (rc == 0) {
        ble_gap_disc_params params = {};
        params.passive = 0;  // Scan responses may contain the device name.
        params.filter_duplicates = 0;
        // Low scan duty cycle leaves airtime for Xiaozhi's Wi-Fi audio.
        params.itvl = 0x100;
        params.window = 0x20;
        rc = ble_gap_disc(address_type, 5000, &params, OnGapEvent, nullptr);
    }
    if (rc != 0) {
        ESP_LOGE(kTag, "Discovery failed: %d", rc);
        Refresh(false);
    }
}

void OnSync() { BeginDiscovery(); }
void HostTask(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}
}  // namespace

void StartZmkScanner() {
    // Kconfig restricts this implementation to the verified Zhengchen board.
    display = static_cast<CodexScannerDisplay*>(Board::GetInstance().GetDisplay());
    Refresh(false);
    const esp_err_t rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(kTag, "NimBLE initialization failed: %s", esp_err_to_name(rc));
        return;
    }
    ble_hs_cfg.sync_cb = OnSync;
    nimble_port_freertos_init(HostTask);
}

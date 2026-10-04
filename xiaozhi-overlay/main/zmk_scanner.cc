#include "zmk_scanner.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <esp_log.h>
#include <esp_timer.h>
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_adv.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>

#include "boards/common/board.h"
#include "display/display.h"
#include "display/lcd_display.h"

#define TAG "ZmkScanner"

namespace {
Display* display;
std::string name_filter = CONFIG_ZMK_SCANNER_NAME_FILTER;
std::vector<ScannerDevice> devices;
int64_t last_display_update_us = 0;
void BeginDiscovery();

void PruneDevices(int64_t now) {
    devices.erase(std::remove_if(devices.begin(), devices.end(), [now](const ScannerDevice& device) {
        return now - device.last_seen_us > 10000000;
    }), devices.end());
}

void RefreshDashboard(bool scanning) {
    auto* lcd = dynamic_cast<LcdDisplay*>(display);
    if (lcd != nullptr) {
        lcd->UpdateScannerDashboard(devices, scanning, name_filter);
    }
}

bool MatchesFilter(const std::string& name) {
    return name_filter.empty() || name.find(name_filter) != std::string::npos;
}

int OnGapEvent(struct ble_gap_event* event, void*) {
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        PruneDevices(esp_timer_get_time());
        RefreshDashboard(true);
        BeginDiscovery();
        return 0;
    }
    if (event->type == BLE_GAP_EVENT_DISC) {
        struct ble_hs_adv_fields fields = {};
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0 ||
            fields.name == nullptr || fields.name_len == 0) {
            return 0;
        }

        std::string name(reinterpret_cast<const char*>(fields.name), fields.name_len);
        if (!MatchesFilter(name)) {
            return 0;
        }

        const int64_t now = esp_timer_get_time();
        const size_t previous_count = devices.size();
        PruneDevices(now);

        char address[18];
        const uint8_t* value = event->disc.addr.val;
        std::snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X",
                      value[5], value[4], value[3], value[2], value[1], value[0]);
        auto existing = std::find_if(devices.begin(), devices.end(), [&address](const ScannerDevice& device) {
            return device.address == address;
        });
        bool should_refresh = devices.size() != previous_count;
        if (existing != devices.end()) {
            should_refresh = should_refresh || std::abs(existing->rssi - event->disc.rssi) >= 4 ||
                             now - last_display_update_us >= 1000000;
            existing->name = name;
            existing->rssi = event->disc.rssi;
            existing->last_seen_us = now;
        } else {
            ScannerDevice device{name, address, event->disc.rssi, now};
            if (devices.size() < 2) {
                devices.push_back(std::move(device));
                should_refresh = true;
            } else {
                auto weakest = std::min_element(devices.begin(), devices.end(), [](const ScannerDevice& a,
                                                                                   const ScannerDevice& b) {
                    return a.rssi < b.rssi;
                });
                if (weakest != devices.end() && event->disc.rssi > weakest->rssi) {
                    *weakest = std::move(device);
                    should_refresh = true;
                }
            }
        }

        if (should_refresh && now - last_display_update_us >= 250000) {
            RefreshDashboard(true);
            last_display_update_us = now;
        }
    }
    return 0;
}

void BeginDiscovery() {
    uint8_t own_address_type;
    int rc = ble_hs_id_infer_auto(0, &own_address_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE address setup failed: %d", rc);
        RefreshDashboard(false);
        return;
    }

    struct ble_gap_disc_params params = {};
    params.passive = 0;
    params.filter_duplicates = 0;
    params.itvl = 0x50;
    params.window = 0x30;
    // Restart periodically so absent devices also expire when no advertisements arrive.
    rc = ble_gap_disc(own_address_type, 5000, &params, OnGapEvent, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE scan start failed: %d", rc);
        RefreshDashboard(false);
    }
}

void OnSync() {
    BeginDiscovery();
}

void NimbleHostTask(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}
}  // namespace

void StartZmkScanner() {
    // The application has already created the Cube board/display and started
    // audio. The scanner updates only the Codex dashboard layer.
    display = Board::GetInstance().GetDisplay();
    RefreshDashboard(true);

    const esp_err_t rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE initialization failed: %s", esp_err_to_name(rc));
        RefreshDashboard(false);
        return;
    }
    ble_svc_gap_device_name_set("Xiaozhi Cube Scanner");
    ble_hs_cfg.sync_cb = OnSync;
    nimble_port_freertos_init(NimbleHostTask);
}

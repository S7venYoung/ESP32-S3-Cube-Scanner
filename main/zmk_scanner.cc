#include "zmk_scanner.h"

#include <cstdio>
#include <algorithm>
#include <string>
#include <vector>

#include <esp_log.h>
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_adv.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>

#include "cube_display.h"

namespace {
constexpr char kTag[] = "ZmkScanner";
CubeDisplay* scanner_display;
std::string name_filter = CONFIG_ZMK_SCANNER_NAME_FILTER;
std::vector<ScannerDevice> devices;

void Show(const std::string& name, const std::string& details) {
    ESP_LOGI(kTag, "%s: %s", name.c_str(), details.c_str());
    scanner_display->Show(name, details);
}

void RefreshDisplay() {
    scanner_display->ShowScanner(devices, true, name_filter);
}

int OnGapEvent(struct ble_gap_event* event, void*) {
    if (event->type != BLE_GAP_EVENT_DISC) return 0;

    struct ble_hs_adv_fields fields = {};
    if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0 ||
        fields.name == nullptr || fields.name_len == 0) return 0;

    std::string name(reinterpret_cast<const char*>(fields.name), fields.name_len);
    if (!name_filter.empty() && name.find(name_filter) == std::string::npos) return 0;

    char address[18] = {};
    const uint8_t* value = event->disc.addr.val;
    std::snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X",
                  value[5], value[4], value[3], value[2], value[1], value[0]);
    auto existing = std::find_if(devices.begin(), devices.end(), [&address](const ScannerDevice& device) {
        return device.address == address;
    });
    if (existing != devices.end()) {
        existing->name = name;
        existing->rssi = event->disc.rssi;
    } else {
        ScannerDevice device{name, address, event->disc.rssi};
        if (devices.size() < 2) {
            devices.push_back(std::move(device));
        } else {
            devices.erase(devices.begin());
            devices.push_back(std::move(device));
        }
    }
    RefreshDisplay();
    return 0;
}

void StartScan() {
    uint8_t own_address_type;
    int rc = ble_hs_id_infer_auto(0, &own_address_type);
    if (rc != 0) {
        Show("BLE ERROR", "Address setup " + std::to_string(rc));
        return;
    }

    struct ble_gap_disc_params params = {};
    params.passive = 0;
    params.filter_duplicates = 1;
    params.itvl = 0x50;
    params.window = 0x30;
    rc = ble_gap_disc(own_address_type, BLE_HS_FOREVER, &params, OnGapEvent, nullptr);
    if (rc != 0) Show("BLE ERROR", "Scan start " + std::to_string(rc));
}

void OnSync() { StartScan(); }

void HostTask(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}
}  // namespace

void StartZmkScanner(CubeDisplay& display) {
    scanner_display = &display;
    scanner_display->ShowScanner(devices, true, name_filter);
    ESP_ERROR_CHECK(nimble_port_init());
    ble_svc_gap_device_name_set("Cube ZMK Scanner");
    ble_hs_cfg.sync_cb = OnSync;
    nimble_port_freertos_init(HostTask);
}

#include "zmk_scanner.h"
#include "codex_scanner_display.h"
#include "board.h"
#include "application.h"
#include "prospector_modifiers.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
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
#include <mutex>

namespace {
constexpr char kTag[] = "ZmkScanner";
CodexScannerDisplay* display = nullptr;
std::vector<ScannerDevice> devices;
const std::string name_filter = CONFIG_ZMK_SCANNER_NAME_FILTER;
int64_t last_refresh_us = 0;
std::string modifier_address;
int64_t modifier_seen_us = -1;
uint8_t modifier_state = 0;
std::mutex fight_mutex;
FightTelemetry fight_data;
int64_t fight_seen_us = -1;
void BeginDiscovery();

void ExpireModifiers(int64_t now) {
    {
        std::lock_guard<std::mutex> lock(fight_mutex);
        if (fight_seen_us>=0 && now-fight_seen_us>3000000) {
            fight_seen_us=-1;
            fight_data={};
            modifier_seen_us=-1;
            modifier_address.clear();
            modifier_state=0;
            Application::GetInstance().Schedule([]() { display->SetMacModifierState(0); });
        }
    }
    if (modifier_seen_us >= 0 && now - modifier_seen_us > 60000000) {
        modifier_seen_us = -1;
        modifier_address.clear();
        modifier_state = 0;
        Application::GetInstance().Schedule([]() { display->SetMacModifierState(0); });
    }
}

void ReceiveModifiers(const ble_gap_event* event, const std::string& address,
                      const std::string& name, int64_t now) {
    if (!name_filter.empty() && name.find(name_filter)==std::string::npos) return;
    const auto* data=event->disc.data;
    const size_t size=event->disc.length_data;
    for (size_t pos=0;pos<size;) {
        const size_t length=data[pos++];
        if (!length || length>size-pos) return;
        FightTelemetry fight;
        if (data[pos]==0xff && ParseFightTelemetry(data+pos+1,length-1,fight)) {
            bool accepted=false;
            {
                std::lock_guard<std::mutex> lock(fight_mutex);
                if (fight_seen_us<0 || now-fight_seen_us>3000000 ||
                    fight_data.keyboard_id==fight.keyboard_id) {
                    fight_data=fight;
                    fight_seen_us=now;
                    accepted=true;
                }
            }
            if (accepted && fight.modifiers!=modifier_state) {
                modifier_state=fight.modifiers;
                modifier_address=address;
                modifier_seen_us=now;
                Application::GetInstance().Schedule([fight]() { display->SetMacModifierState(fight.modifiers); });
            } else if (accepted) modifier_seen_us=now;
        }
        uint8_t mods=0;
        bool fight_active;
        { std::lock_guard<std::mutex> lock(fight_mutex); fight_active=fight_seen_us>=0 && now-fight_seen_us<=3000000; }
        if (!fight_active && (modifier_address.empty() || modifier_address==address) &&
            data[pos]==0xff && ParseProspectorModifiers(data+pos+1,length-1,mods)) {
            modifier_address=address;
            modifier_seen_us=now;
            if (mods!=modifier_state) {
                modifier_state=mods;
                Application::GetInstance().Schedule([mods]() { display->SetMacModifierState(mods); });
            }
        }
        pos+=length;
    }
}

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
        ExpireModifiers(esp_timer_get_time());
        Prune(esp_timer_get_time());
        Refresh(true);
        BeginDiscovery();
        return 0;
    }
    if (event->type != BLE_GAP_EVENT_DISC) return 0;
    ble_hs_adv_fields fields = {};
    if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) return 0;
    const int64_t now = esp_timer_get_time();
    Prune(now);
    const auto* addr = event->disc.addr.val;
    char address[18];
    std::snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X",
                  addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    auto existing = std::find_if(devices.begin(), devices.end(), [&address](const ScannerDevice& d) {
        return d.address == address;
    });
    const std::string name = fields.name && fields.name_len
        ? std::string(reinterpret_cast<const char*>(fields.name),fields.name_len)
        : existing!=devices.end() ? existing->name : "";
    ExpireModifiers(now);
    ReceiveModifiers(event,address,name,now);
    if (name.empty() || (!name_filter.empty() && name.find(name_filter)==std::string::npos)) return 0;
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

FightTelemetry GetFightTelemetry() {
    std::lock_guard<std::mutex> lock(fight_mutex);
    if (fight_seen_us<0 || esp_timer_get_time()-fight_seen_us>3000000) return {};
    return fight_data;
}

void StartZmkScanner() {
    // Discovery is optional (it currently supplies names/RSSI, not keyboard
    // telemetry). Protect native HTTP/audio task stacks and DMA allocations.
    const auto caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const size_t free = heap_caps_get_free_size(caps);
    const size_t largest = heap_caps_get_largest_free_block(caps);
    ESP_LOGI(kTag, "Before BLE: internal free=%u largest=%u",
             static_cast<unsigned>(free), static_cast<unsigned>(largest));
    if (free < 64 * 1024 || largest < 16 * 1024) {
        ESP_LOGW(kTag, "Skipping optional BLE discovery to preserve native services");
        return;
    }
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

#include "codex_scanner_display.h"

#include "application.h"
#include "board.h"
#include <esp_timer.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

LV_FONT_DECLARE(font_puhui_14_1);

namespace {
constexpr uint32_t kInk = 0x101411;
constexpr uint32_t kCard = 0x1E2520;
constexpr uint32_t kYellow = 0xFFBF18;
constexpr uint32_t kPaper = 0xF3EEE5;
constexpr uint32_t kMuted = 0x7C847D;
constexpr uint32_t kGreen = 0x58E85D;
constexpr uint32_t kRed = 0xE23B2E;

lv_obj_t* Panel(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, int radius = 7) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t* Text(lv_obj_t* parent, const char* text, int x, int y, int w, uint32_t color) {
    auto* obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, font_puhui_14_1.line_height);
    lv_obj_set_style_text_font(obj, &font_puhui_14_1, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}
}  // namespace

void CodexScannerDisplay::SetupUI() {
    // The SPI display constructor has already created the native UI. Retain
    // its status objects and warning popups for the original battery service.
    ZHENGCHEN_LcdDisplay::SetupUI();
    DisplayLockGuard lock(this);
    if (dashboard_ != nullptr) return;
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    dashboard_ = Panel(lv_screen_active(), 0, 0, width_, height_, kInk, 0);
    auto* header = Panel(dashboard_, 8, 8, 224, 38, kYellow);
    for (int i = 0; i < 2; ++i) {
        const int x = 10 + i * 23;
        Panel(header, x, 8, 20, 22, kInk, 5);
        Panel(header, x + 5, 12, 10, 12, kYellow, 5);
        Panel(header, x + 8, 16, 4, 4, kInk, 2);
    }
    Text(header, "WALLE // CODEX", 60, 11, 135, kInk);
    assistant_dot_ = Panel(header, 207, 14, 9, 9, kMuted, 5);

    for (int i = 0; i < 2; ++i) {
        auto* card = Panel(dashboard_, 8, 54 + i * 64, 224, 58, kCard);
        Panel(card, 7, 19, 20, 20, kYellow, 10);
        Text(card, i == 0 ? "1" : "2", 13, 21, 12, kInk);
        device_names_[i] = Text(card, i == 0 ? "SEARCHING" : "WAITING FOR PEER", 34, 5, 181, kPaper);
        addresses_[i] = Text(card, "ZMK BLE ADVERTISEMENT", 34, 23, 181, kMuted);
        rssi_labels_[i] = Text(card, "-- dBm", 34, 41, 105, kGreen);
        for (int j = 0; j < 5; ++j) {
            const int h = 4 + j * 2;
            bars_[i][j] = Panel(card, 180 + j * 7, 52 - h, 5, h, kMuted, 1);
        }
    }

    auto* footer = Panel(dashboard_, 8, 184, 224, 48, kCard);
    scan_dot_ = Panel(footer, 8, 9, 7, 7, kGreen, 4);
    scan_status_ = Text(footer, "STARTING", 21, 5, 116, kPaper);
    battery_text_ = Text(footer, "BAT --%", 143, 5, 75, kYellow);
    filter_label_ = Text(footer, "FILTER: ALL", 8, 27, 208, kMuted);

    notice_panel_ = Panel(dashboard_, 8, 181, 224, 51, kYellow);
    notice_text_ = Text(notice_panel_, "", 7, 5, 210, kInk);
    lv_obj_set_height(notice_text_, 43);
    lv_obj_add_flag(notice_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(low_battery_popup_);
    lv_obj_move_foreground(high_temp_popup_);
    UpdateAssistantDot();
}

void CodexScannerDisplay::UpdateAssistantDot() {
    if (assistant_dot_ == nullptr) return;
    uint32_t color = kMuted;
    switch (Application::GetInstance().GetDeviceState()) {
        case kDeviceStateListening: color = kYellow; break;
        case kDeviceStateSpeaking: color = kGreen; break;
        case kDeviceStateConnecting: color = 0x49A5F0; break;
        case kDeviceStateWifiConfiguring:
        case kDeviceStateActivating: color = 0xFF902B; break;
        case kDeviceStateFatalError: color = kRed; break;
        default: break;
    }
    lv_obj_set_style_bg_color(assistant_dot_, lv_color_hex(color), 0);
}

void CodexScannerDisplay::SetStatus(const char* status) {
    if (dashboard_ == nullptr) {
        Display::SetStatus(status);
        return;
    }
    DisplayLockGuard lock(this);
    UpdateAssistantDot();
}

void CodexScannerDisplay::SetChatMessage(const char* role, const char* content) {
    // Keep spoken conversation off the dashboard, while retaining important
    // first-use Wi-Fi and activation instructions from the native app.
    if (role == nullptr || content == nullptr || content[0] == '\0' || std::strcmp(role, "system") != 0) return;
    const auto state = Application::GetInstance().GetDeviceState();
    if (state != kDeviceStateIdle && state != kDeviceStateListening && state != kDeviceStateSpeaking) {
        ShowNotification(content, 10000);
    }
}

void CodexScannerDisplay::ShowNotification(const std::string& text, int duration_ms) {
    ShowNotification(text.c_str(), duration_ms);
}

void CodexScannerDisplay::ShowNotification(const char* text, int duration_ms) {
    if (text == nullptr || notice_panel_ == nullptr) return;
    DisplayLockGuard lock(this);
    lv_label_set_text(notice_text_, text);
    notice_until_us_ = esp_timer_get_time() + static_cast<int64_t>(std::max(0, duration_ms)) * 1000;
    lv_obj_remove_flag(notice_panel_, LV_OBJ_FLAG_HIDDEN);
}

void CodexScannerDisplay::Update() {
    // Native battery warnings, charging detection, and audio remain active.
    Display::Update();
    int level = 0;
    bool charging = false, discharging = false;
    auto& board = Board::GetInstance();
    const bool has_battery = board.GetBatteryLevel(level, charging, discharging);
    DisplayLockGuard lock(this);
    if (dashboard_ == nullptr) return;
    UpdateAssistantDot();
    char text[24];
    if (has_battery) {
        std::snprintf(text, sizeof(text), "%s%d%%", charging ? "+ " : "BAT ", level);
        lv_label_set_text(battery_text_, text);
    }
    if (esp_timer_get_time() >= notice_until_us_) {
        lv_obj_add_flag(notice_panel_, LV_OBJ_FLAG_HIDDEN);
    }
}

void CodexScannerDisplay::UpdateScanner(const std::vector<ScannerDevice>& devices, bool scanning,
                                      const std::string& filter) {
    DisplayLockGuard lock(this);
    if (dashboard_ == nullptr) return;
    for (size_t i = 0; i < 2; ++i) {
        const bool found = i < devices.size();
        lv_label_set_text(device_names_[i], found ? devices[i].name.c_str() : (i == 0 ? "SEARCHING" : "WAITING FOR PEER"));
        lv_label_set_text(addresses_[i], found ? devices[i].address.c_str() : "ZMK BLE ADVERTISEMENT");
        char rssi[24];
        if (found) std::snprintf(rssi, sizeof(rssi), "%d dBm", devices[i].rssi);
        else std::snprintf(rssi, sizeof(rssi), "-- dBm");
        lv_label_set_text(rssi_labels_[i], rssi);
        const int strength = found ? std::clamp((devices[i].rssi + 100) * 5 / 65, 0, 5) : 0;
        for (int j = 0; j < 5; ++j) {
            lv_obj_set_style_bg_color(bars_[i][j], lv_color_hex(j < strength ? kGreen : kMuted), 0);
        }
    }
    lv_label_set_text(scan_status_, scanning ? "SCAN ACTIVE" : "BLE ERROR");
    lv_obj_set_style_bg_color(scan_dot_, lv_color_hex(scanning ? kGreen : kRed), 0);
    lv_label_set_text(filter_label_, ("FILTER: " + (filter.empty() ? std::string("ALL") : filter)).c_str());
}

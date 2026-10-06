#include "codex_scanner_display.h"

#include "application.h"
#include "board.h"
#include "codex_metrics.h"
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
    auto* header = Panel(dashboard_, 6, 6, 228, 33, kInk);
    Text(header, "CODEX // SOFLE", 2, 8, 145, kYellow);
    scan_dot_ = Panel(header, 145, 12, 6, 6, kMuted, 4);
    scan_status_ = Text(header, "OFFLINE", 155, 8, 66, kPaper);
    assistant_dot_ = Panel(dashboard_, 224, 2, 7, 7, kMuted, 4);
    Panel(dashboard_, 8, 40, 224, 1, kMuted, 0);

    auto* left = Panel(dashboard_, 6, 47, 110, 130, kCard);
    Text(left, "5 HOUR LEFT", 6, 7, 101, kPaper);
    quota_text_ = Text(left, "--", 7, 33, 102, kYellow);
    lv_obj_set_style_text_font(quota_text_, &lv_font_montserrat_32, 0);
    lv_obj_set_height(quota_text_, 40);
    Panel(left, 6, 80, 98, 1, kMuted, 0);
    Text(left, "7 DAY", 6, 87, 50, kPaper);
    week_text_ = Text(left, "--", 58, 87, 49, kYellow);
    Panel(left, 6, 112, 98, 7, kMuted, 2);
    quota_bar_ = Panel(left, 6, 112, 1, 7, kYellow, 2);
    lv_obj_add_flag(quota_bar_, LV_OBJ_FLAG_HIDDEN);

    auto* right = Panel(dashboard_, 122, 47, 112, 130, kCard);
    Text(right, "TODAY TOTAL", 6, 7, 104, kPaper);
    tokens_text_ = Text(right, "--", 5, 43, 106, kPaper);
    lv_obj_set_style_text_font(tokens_text_, &lv_font_montserrat_24, 0);
    lv_obj_set_height(tokens_text_, 32);
    Text(right, "TOKENS", 6, 88, 100, kMuted);

    auto* footer = Panel(dashboard_, 6, 184, 228, 50, kCard);
    Text(footer, "LAYER --  WPM --", 5, 5, 155, kPaper);
    battery_text_ = Text(footer, "BAT --%", 161, 5, 65, kYellow);
    Text(footer, "L --%       R --%", 5, 27, 215, kMuted);

    notice_panel_ = Panel(dashboard_, 8, 181, 224, 51, kYellow);
    notice_text_ = Text(notice_panel_, "", 7, 5, 210, kInk);
    lv_obj_set_height(notice_text_, 43);
    lv_obj_add_flag(notice_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(low_battery_popup_);
    lv_obj_move_foreground(high_temp_popup_);
    UpdateAssistantDot();
    UpdateMetrics();
}

void CodexScannerDisplay::UpdateMetrics() {
    if (quota_text_ == nullptr) return;
    const auto sample = GetCodexSnapshot();
    lv_label_set_text(scan_status_, sample.transport);
    lv_obj_set_style_bg_color(scan_dot_, lv_color_hex(sample.online ? kGreen : kMuted), 0);
    char text[32];
    if (sample.online && sample.metrics.left >= 0) std::snprintf(text, sizeof(text), "%d%%", sample.metrics.left);
    else std::snprintf(text, sizeof(text), "--");
    lv_label_set_text(quota_text_, text);
    if (sample.online && sample.metrics.week_left >= 0) {
        std::snprintf(text, sizeof(text), "%d%%", sample.metrics.week_left);
        lv_obj_set_width(quota_bar_, std::max(1, sample.metrics.week_left * 98 / 100));
        if (sample.metrics.week_left > 0) lv_obj_remove_flag(quota_bar_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(quota_bar_, LV_OBJ_FLAG_HIDDEN);
    } else {
        std::snprintf(text, sizeof(text), "--");
        lv_obj_add_flag(quota_bar_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(week_text_, text);
    if (!sample.online || sample.metrics.tokens < 0) std::snprintf(text, sizeof(text), "--");
    else if (sample.metrics.tokens >= 1000000) std::snprintf(text, sizeof(text), "%.1fM", sample.metrics.tokens / 1000000.0);
    else if (sample.metrics.tokens >= 1000) std::snprintf(text, sizeof(text), "%.1fK", sample.metrics.tokens / 1000.0);
    else std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(sample.metrics.tokens));
    lv_label_set_text(tokens_text_, text);
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
    UpdateMetrics();
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
    // BLE discovery alone cannot supply layers/WPM/batteries. Keep those
    // fields unavailable until the real ZMK telemetry protocol is connected.
    (void)devices; (void)scanning; (void)filter;
}

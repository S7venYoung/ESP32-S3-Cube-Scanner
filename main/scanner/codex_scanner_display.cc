#include "codex_scanner_display.h"

#include "application.h"
#include "board.h"
#include "codex_metrics.h"
#include "codex_draw.h"
#include <esp_timer.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

LV_FONT_DECLARE(font_puhui_14_1);

namespace {
constexpr uint32_t kInk = 0x101411;
constexpr uint32_t kCard = 0x101411;
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

lv_obj_t* Impact(lv_obj_t* parent, const char* text, int x, int y, int w,
                 uint32_t color, int size = 16, bool centered = false) {
    return CodexDrawText(parent, text, x, y, w, color, size, centered);
}

lv_obj_t* Outline(lv_obj_t* parent, int x, int y, int w, int h, uint32_t border, int radius = 10) {
    auto* obj = Panel(parent, x, y, w, h, kCard, radius);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
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
    Impact(dashboard_, "CODEX", 10, 9, 56, kYellow, 20);
    Impact(dashboard_, "// SOFLE", 68, 9, 87, kPaper, 20);
    scan_dot_ = Panel(dashboard_, 226, 16, 7, 7, kMuted, 4);
    scan_status_ = Impact(dashboard_, "OFFLINE", 169, 12, 53, kPaper);
    assistant_dot_ = Panel(dashboard_, 230, 3, 5, 5, kMuted, 3);
    Panel(dashboard_, 10, 36, 220, 2, kYellow, 0);

    auto* left = Outline(dashboard_, 6, 44, 110, 126, kYellow, 12);
    Impact(left, "5 HOUR LEFT", 6, 6, 98, kPaper);
    quota_text_ = Impact(left, "--%", 1, 26, 106, kYellow, 48, true);
    Panel(left, 6, 82, 96, 1, kMuted, 0);
    Impact(left, "7 DAY LEFT", 6, 87, 61, kPaper);
    week_text_ = Impact(left, "--%", 66, 84, 40, kYellow, 20);
    for (int i = 0; i < 6; ++i) {
        week_segments_[i] = Panel(left, 6 + i * 16, 111, 14, 6, kMuted, 1);
    }

    auto* right = Outline(dashboard_, 122, 44, 112, 126, kYellow, 12);
    Impact(right, "TODAY TOTAL", 6, 6, 100, kPaper);
    tokens_text_ = Impact(right, "--", 2, 38, 106, kPaper, 48, true);
    Impact(right, "TOKENS", 6, 98, 98, kMuted, 16, true);

    auto* state = Outline(dashboard_, 6, 177, 228, 25, kMuted, 7);
    Impact(state, "LAYER", 7, 4, 42, kMuted);
    Impact(state, "--", 52, 2, 55, kYellow, 20);
    Panel(state, 118, 4, 1, 16, kMuted, 0);
    Impact(state, "WPM", 132, 4, 35, kMuted);
    Impact(state, "--", 177, 2, 40, kYellow, 20);
    for (int i = 0; i < 2; ++i) {
        auto* battery = Outline(dashboard_, i == 0 ? 6 : 122, 208, i == 0 ? 110 : 112, 26, kMuted, 7);
        Impact(battery, i == 0 ? "L" : "R", 7, 4, 13, kMuted);
        Impact(battery, "--%", 24, 4, 35, kPaper);
        Outline(battery, 66, 8, 33, 10, kMuted, 2);
        Panel(battery, 99, 11, 2, 4, kMuted, 0);
    }

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
    CodexDrawSetText(scan_status_, sample.transport);
    lv_obj_set_style_bg_color(scan_dot_, lv_color_hex(sample.online ? kGreen : kMuted), 0);
    char text[32];
    if (sample.online && sample.metrics.left >= 0) std::snprintf(text, sizeof(text), "%d%%", sample.metrics.left);
    else std::snprintf(text, sizeof(text), "--%%");
    CodexDrawSetText(quota_text_, text);
    if (sample.online && sample.metrics.week_left >= 0) {
        std::snprintf(text, sizeof(text), "%d%%", sample.metrics.week_left);
    } else {
        std::snprintf(text, sizeof(text), "--%%");
    }
    CodexDrawSetText(week_text_, text);
    const int segments = sample.online && sample.metrics.week_left >= 0 ?
        (sample.metrics.week_left * 6 + 99) / 100 : 0;
    for (int i = 0; i < 6; ++i) lv_obj_set_style_bg_color(week_segments_[i],
        lv_color_hex(i < segments ? kYellow : kMuted), 0);
    if (!sample.online || sample.metrics.tokens < 0) std::snprintf(text, sizeof(text), "--");
    else if (sample.metrics.tokens >= 1000000) std::snprintf(text, sizeof(text), "%.1fM", sample.metrics.tokens / 1000000.0);
    else if (sample.metrics.tokens >= 1000) std::snprintf(text, sizeof(text), "%.1fK", sample.metrics.tokens / 1000.0);
    else std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(sample.metrics.tokens));
    CodexDrawSetText(tokens_text_, text);
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
    if (has_battery && battery_text_ != nullptr) {
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

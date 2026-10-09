#pragma once

#include "display/lcd_display.h"
#include "scanner_device.h"

#include <vector>

class CodexScannerDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;
    void SetupUI() override;
    void SetDashboardTheme(const std::string& theme);
    void RegisterDashboardTools();
    void NextDashboardTheme();
    // Bits 0..3: Control, Option, Command, Shift. Feed only real telemetry.
    void SetMacModifierState(uint8_t modifiers);
    void UpdateStatusBar(bool update_all = false) override;
    void ClearChatMessages() override {}
    void SetStatus(const char* status) override;
    void SetEmotion(const char* emotion) override {}
    void SetChatMessage(const char* role, const char* content) override;
    void ShowNotification(const char* text, int duration_ms = 3000) override;
    void ShowNotification(const std::string& text, int duration_ms = 3000) override;
    void UpdateScanner(const std::vector<ScannerDevice>& devices, bool scanning,
                       const std::string& filter);

protected:
    lv_obj_t* high_temp_popup_ = nullptr;

private:
    void UpdateAssistantOverlay();  // Caller holds the LVGL lock.
    void UpdateMetrics();
    void SetupMacintosh();
    void UpdateMacintosh();
    bool mac_theme_ = false;
    std::string dashboard_theme_ = "codex";
    lv_obj_t* arcade_dashboard_ = nullptr;
    lv_obj_t* mac_dashboard_ = nullptr;
    lv_obj_t* mac_transport_ = nullptr;
    lv_obj_t* mac_quota_ = nullptr;
    lv_obj_t* mac_tokens_ = nullptr;
    lv_obj_t* mac_clock_ = nullptr;
    lv_obj_t* mac_battery_ = nullptr;
    lv_obj_t* mac_wpm_ = nullptr;
    int shown_mac_minute_ = -1;
    lv_obj_t* mac_modifier_keys_[4] = {};
    lv_obj_t* mac_modifier_symbols_[4] = {};
    lv_obj_t* quota_text_ = nullptr;
    lv_obj_t* week_text_ = nullptr;
    lv_obj_t* tokens_text_ = nullptr;
    lv_obj_t* week_segments_[6] = {};
    lv_obj_t* dashboard_ = nullptr;
    lv_obj_t* assistant_overlay_ = nullptr;
    lv_obj_t* assistant_orb_ = nullptr;
    lv_obj_t* assistant_overlay_text_ = nullptr;
    int shown_assistant_state_ = -1;
    lv_obj_t* scan_dot_ = nullptr;
    lv_obj_t* scan_status_ = nullptr;
    lv_obj_t* filter_label_ = nullptr;
    lv_obj_t* battery_text_ = nullptr;
    lv_obj_t* cube_battery_fill_ = nullptr;
    int shown_battery_level_ = -2;
    bool shown_charging_ = false;
    lv_obj_t* device_names_[2] = {};
    lv_obj_t* addresses_[2] = {};
    lv_obj_t* rssi_labels_[2] = {};
    lv_obj_t* bars_[2][5] = {};
    lv_obj_t* notice_panel_ = nullptr;
    lv_obj_t* notice_text_ = nullptr;
    int64_t notice_until_us_ = 0;
};

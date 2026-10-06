#pragma once

#include "boards/zhengchen-1.54tft-wifi/zhengchen_lcd_display.h"
#include "scanner_device.h"

#include <vector>

class CodexScannerDisplay : public ZHENGCHEN_LcdDisplay {
public:
    using ZHENGCHEN_LcdDisplay::ZHENGCHEN_LcdDisplay;
    void SetupUI();
    void SetStatus(const char* status) override;
    void SetEmotion(const char* emotion) override {}
    void SetIcon(const char* icon) override {}
    void SetChatMessage(const char* role, const char* content) override;
    void ShowNotification(const char* text, int duration_ms = 3000) override;
    void ShowNotification(const std::string& text, int duration_ms = 3000) override;
    void UpdateScanner(const std::vector<ScannerDevice>& devices, bool scanning,
                       const std::string& filter);

protected:
    void Update() override;

private:
    void UpdateAssistantDot();  // Caller holds the LVGL lock.
    void UpdateMetrics();
    lv_obj_t* quota_text_ = nullptr;
    lv_obj_t* week_text_ = nullptr;
    lv_obj_t* tokens_text_ = nullptr;
    lv_obj_t* quota_bar_ = nullptr;
    lv_obj_t* dashboard_ = nullptr;
    lv_obj_t* assistant_dot_ = nullptr;
    lv_obj_t* scan_dot_ = nullptr;
    lv_obj_t* scan_status_ = nullptr;
    lv_obj_t* filter_label_ = nullptr;
    lv_obj_t* battery_text_ = nullptr;
    lv_obj_t* device_names_[2] = {};
    lv_obj_t* addresses_[2] = {};
    lv_obj_t* rssi_labels_[2] = {};
    lv_obj_t* bars_[2][5] = {};
    lv_obj_t* notice_panel_ = nullptr;
    lv_obj_t* notice_text_ = nullptr;
    int64_t notice_until_us_ = 0;
};

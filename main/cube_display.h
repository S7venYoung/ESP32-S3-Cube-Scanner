#pragma once

#include <string>
#include <vector>

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

struct ScannerDevice {
    std::string name;
    std::string address;
    int rssi = 0;
};

class CubeDisplay {
public:
    void Initialize();
    void Show(const std::string& title, const std::string& detail);
    void ShowScanner(const std::vector<ScannerDevice>& devices, bool scanning,
                     const std::string& name_filter);

private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
};

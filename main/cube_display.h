#pragma once

#include <string>

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

class CubeDisplay {
public:
    void Initialize();
    void Show(const std::string& title, const std::string& detail);

private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
};

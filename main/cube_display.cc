#include "cube_display.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_check.h>
#include <esp_lcd_panel_vendor.h>

namespace {
constexpr gpio_num_t kMosi = GPIO_NUM_10;
constexpr gpio_num_t kSclk = GPIO_NUM_9;
constexpr gpio_num_t kDc = GPIO_NUM_8;
constexpr gpio_num_t kCs = GPIO_NUM_14;
constexpr gpio_num_t kReset = GPIO_NUM_18;
constexpr gpio_num_t kBacklight = GPIO_NUM_13;
constexpr int kWidth = 292;
constexpr int kHeight = 240;

// Small built-in 5x7 font for uppercase ASCII, digits and punctuation.
struct Glyph { char c; uint8_t rows[7]; };
constexpr Glyph kFont[] = {
    {' ',{0,0,0,0,0,0,0}}, {'-',{0,0,0,31,0,0,0}}, {'.',{0,0,0,0,0,12,12}}, {':',{0,12,12,0,12,12,0}},
    {'?',{14,17,1,2,4,0,4}},
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}}, {'2',{14,17,1,2,4,8,31}},
    {'3',{30,1,1,14,1,1,30}}, {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}}, {'8',{14,17,17,14,17,17,14}},
    {'9',{14,17,17,15,1,1,14}}, {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{28,18,17,17,17,18,28}}, {'E',{31,16,16,30,16,16,31}},
    {'F',{31,16,16,30,16,16,16}}, {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,2,18,12}}, {'K',{17,18,20,24,20,18,17}},
    {'L',{16,16,16,16,16,16,31}}, {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}}, {'Q',{14,17,17,17,21,18,13}},
    {'R',{30,17,17,30,20,18,17}}, {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}}, {'W',{17,17,17,21,21,21,10}},
    {'X',{17,17,10,4,10,17,17}}, {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
};

const Glyph* FindGlyph(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    for (const auto& glyph : kFont) if (glyph.c == c) return &glyph;
    return FindGlyph('?');
}

void DrawText(uint16_t* pixels, int x, int y, const std::string& text, uint16_t color, int scale) {
    int origin_x = x;
    for (char c : text) {
        if (c == '\n' || x > kWidth - 7 * scale) {
            x = origin_x;
            y += 10 * scale;
            if (c == '\n') continue;
        }
        const Glyph* glyph = FindGlyph(c);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((glyph->rows[row] & (1 << (4 - col))) == 0) continue;
                for (int dy = 0; dy < scale; ++dy) {
                    for (int dx = 0; dx < scale; ++dx) {
                        const int px = x + col * scale + dx;
                        const int py = y + row * scale + dy;
                        if (px >= 0 && px < kWidth && py >= 0 && py < kHeight)
                            pixels[py * kWidth + px] = color;
                    }
                }
            }
        }
        x += 6 * scale;
    }
}
}  // namespace

void CubeDisplay::Initialize() {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = kMosi;
    bus.miso_io_num = GPIO_NUM_NC;
    bus.sclk_io_num = kSclk;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = kWidth * kHeight * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = kCs;
    io.dc_gpio_num = kDc;
    io.spi_mode = 3;
    io.pclk_hz = 40 * 1000 * 1000;
    io.trans_queue_depth = 10;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io, &panel_io_));

    esp_lcd_panel_dev_config_t config = {};
    config.reset_gpio_num = kReset;
    config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    config.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io_, &config, &panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, false, true));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true));
    ESP_ERROR_CHECK(gpio_set_direction(kBacklight, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(kBacklight, 1));
}

void CubeDisplay::Show(const std::string& title, const std::string& detail) {
    std::vector<uint16_t> pixels(kWidth * kHeight, 0x0000);
    DrawText(pixels.data(), 12, 12, title, 0x07FF, 2);
    DrawText(pixels.data(), 12, 48, detail, 0xFFFF, 2);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_, 0, 0, kWidth, kHeight, pixels.data()));
}


#include "cube_display.h"

#include <algorithm>
#include <cmath>
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

constexpr uint16_t Rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t kInk = Rgb565(16, 20, 17);
constexpr uint16_t kYellow = Rgb565(255, 191, 24);
constexpr uint16_t kPaper = Rgb565(243, 238, 229);
constexpr uint16_t kMuted = Rgb565(109, 116, 110);
constexpr uint16_t kGreen = Rgb565(88, 232, 93);
constexpr uint16_t kCard = Rgb565(30, 37, 32);

// Small built-in 5x7 font for uppercase ASCII, digits and punctuation.
struct Glyph { char c; uint8_t rows[7]; };
constexpr Glyph kFont[] = {
    {' ',{0,0,0,0,0,0,0}}, {'-',{0,0,0,31,0,0,0}}, {'.',{0,0,0,0,0,12,12}}, {':',{0,12,12,0,12,12,0}},
    {'?',{14,17,1,2,4,0,4}},
    {'/',{1,1,2,4,8,16,16}}, {'%',{17,2,4,8,17,0,0}}, {'=',{0,0,31,0,31,0,0}},
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

void FillRect(uint16_t* pixels, int x, int y, int w, int h, uint16_t color) {
    const int x0 = std::max(0, x), y0 = std::max(0, y);
    const int x1 = std::min(kWidth, x + w), y1 = std::min(kHeight, y + h);
    for (int py = y0; py < y1; ++py)
        std::fill(pixels + py * kWidth + x0, pixels + py * kWidth + x1, color);
}

void FillRoundRect(uint16_t* pixels, int x, int y, int w, int h, int radius, uint16_t color) {
    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            const int cx = px < radius ? radius : (px >= w - radius ? w - radius - 1 : px);
            const int cy = py < radius ? radius : (py >= h - radius ? h - radius - 1 : py);
            const int dx = px - cx, dy = py - cy;
            if (dx * dx + dy * dy <= radius * radius)
                pixels[(y + py) * kWidth + x + px] = color;
        }
    }
}

void StrokeRoundRect(uint16_t* pixels, int x, int y, int w, int h, int radius,
                     int thickness, uint16_t color) {
    FillRoundRect(pixels, x, y, w, h, radius, color);
    FillRoundRect(pixels, x + thickness, y + thickness, w - 2 * thickness,
                  h - 2 * thickness, std::max(1, radius - thickness), kCard);
}

void FillCircle(uint16_t* pixels, int cx, int cy, int radius, uint16_t color) {
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= radius * radius) {
                const int px = cx + x, py = cy + y;
                if (px >= 0 && px < kWidth && py >= 0 && py < kHeight)
                    pixels[py * kWidth + px] = color;
            }
        }
    }
}

void CircleOutline(uint16_t* pixels, int cx, int cy, int radius, int thickness, uint16_t color) {
    const int outer = radius * radius;
    const int inner = std::max(0, radius - thickness) * std::max(0, radius - thickness);
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            const int distance = x * x + y * y;
            const int px = cx + x, py = cy + y;
            if (distance <= outer && distance >= inner && px >= 0 && px < kWidth &&
                py >= 0 && py < kHeight) pixels[py * kWidth + px] = color;
        }
    }
}

void DrawEye(uint16_t* pixels, int x) {
    FillRoundRect(pixels, x, 12, 27, 25, 8, kInk);
    FillCircle(pixels, x + 13, 24, 8, kYellow);
    FillCircle(pixels, x + 13, 24, 3, kInk);
}

void DrawSignalBars(uint16_t* pixels, int x, int y, int rssi) {
    const int level = std::clamp((rssi + 100) * 5 / 65, 0, 5);
    for (int i = 0; i < 5; ++i) {
        const int height = 3 + i * 2;
        FillRoundRect(pixels, x + i * 7, y + 10 - height, 4, height, 1,
                      i < level ? (rssi < -82 ? kYellow : kGreen) : kMuted);
    }
}

std::string Truncate(const std::string& value, size_t max_chars) {
    if (value.size() <= max_chars) return value;
    if (max_chars < 4) return value.substr(0, max_chars);
    return value.substr(0, max_chars - 3) + "...";
}

void DrawHeader(uint16_t* pixels) {
    FillRoundRect(pixels, 9, 6, 274, 40, 11, kYellow);
    DrawEye(pixels, 17);
    DrawEye(pixels, 49);
    DrawText(pixels, 91, 18, "WALLE // CODEX", kInk, 2);
}

void DrawDeviceCard(uint16_t* pixels, const ScannerDevice& device, int index, int y) {
    FillRoundRect(pixels, 10, y, 272, 51, 9, kMuted);
    FillRoundRect(pixels, 11, y + 1, 270, 49, 8, kCard);
    FillRoundRect(pixels, 17, y + 12, 23, 23, 5, kYellow);
    DrawText(pixels, 24, y + 17, std::to_string(index + 1), kInk, 1);
    DrawText(pixels, 49, y + 8, Truncate(device.name, 12), kPaper, 2);
    DrawText(pixels, 49, y + 31, device.address, kMuted, 1);

    const std::string rssi = std::to_string(device.rssi);
    DrawText(pixels, 218, y + 9, rssi, device.rssi < -82 ? kYellow : kGreen, 2);
    DrawText(pixels, 268, y + 14, "D", kMuted, 1);
    DrawSignalBars(pixels, 214, y + 34, device.rssi);
}

void DrawRadar(uint16_t* pixels) {
    CircleOutline(pixels, 146, 105, 38, 2, kMuted);
    CircleOutline(pixels, 146, 105, 27, 2, kMuted);
    CircleOutline(pixels, 146, 105, 15, 2, kMuted);
    FillCircle(pixels, 146, 105, 4, kYellow);
    for (int i = 0; i < 26; ++i) {
        const int x = 146 + i;
        const int y = 105 - i / 2;
        FillCircle(pixels, x, y, 2, (i % 4 == 0) ? kYellow : kGreen);
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

void CubeDisplay::ShowScanner(const std::vector<ScannerDevice>& devices, bool scanning,
                              const std::string& name_filter) {
    std::vector<uint16_t> pixels(kWidth * kHeight, kInk);
    DrawHeader(pixels.data());

    if (devices.empty()) {
        DrawRadar(pixels.data());
        DrawText(pixels.data(), 55, 144, scanning ? "SEARCHING" : "SCAN PAUSED", kPaper, 2);
        DrawText(pixels.data(), 67, 163, "FOR ZMK NODES", kYellow, 1);
    } else {
        DrawDeviceCard(pixels.data(), devices[0], 0, 54);
        if (devices.size() > 1) {
            DrawDeviceCard(pixels.data(), devices[1], 1, 111);
        } else {
            FillRoundRect(pixels.data(), 10, 111, 272, 51, 9, kMuted);
            FillRoundRect(pixels.data(), 11, 112, 270, 49, 8, kCard);
            DrawText(pixels.data(), 27, 132, "WAITING FOR PEER", kMuted, 2);
        }
    }

    FillRoundRect(pixels.data(), 10, 174, 272, 54, 7, kCard);
    FillCircle(pixels.data(), 23, 190, 5, scanning ? kGreen : kYellow);
    DrawText(pixels.data(), 35, 184, scanning ? "SCANNING" : "PAUSED", kPaper, 2);
    DrawText(pixels.data(), 213, 187, "ZMK BLE", kYellow, 1);
    const std::string filter = name_filter.empty() ? "ALL NAMED DEVICES" : Truncate(name_filter, 29);
    DrawText(pixels.data(), 18, 209, "FILTER: " + filter, kMuted, 1);

    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_, 0, 0, kWidth, kHeight, pixels.data()));
}

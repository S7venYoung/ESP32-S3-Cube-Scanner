#include "codex_draw.h"
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include "codex_draw_masks.h"

namespace {
struct Drawing {
    std::string text;
    uint16_t* pixels = nullptr;
    int width = 0, height = 0;
    int stride = 0;
    uint32_t color = 0;
    const DrawMask* mask = nullptr;
    bool centered = false;
};
constexpr uint32_t background = 0x101411;
uint16_t Rgb565(uint32_t rgb) {
    return ((rgb >> 19) & 31) << 11 | ((rgb >> 10) & 63) << 5 | ((rgb >> 3) & 31);
}
uint32_t Mix(uint32_t foreground, unsigned alpha) {
    uint32_t rgb = 0;
    for (int shift : {16, 8, 0}) {
        unsigned a = (foreground >> shift) & 255, b = (background >> shift) & 255;
        rgb |= ((a * alpha + b * (255 - alpha) + 127) / 255) << shift;
    }
    return rgb;
}
int Width(const DrawMask& mask, const char* text) {
    unsigned advance = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        if (*p >= 32 && *p <= 126)
            advance += mask.glyphs[*p - 31].advance;
    }
    return (advance + 8) / 16;
}
void Render(lv_obj_t* object, Drawing& drawing) {
    const auto& mask = *drawing.mask;
    const int natural_width = Width(mask, drawing.text.c_str());
    // Shrink outlines proportionally when 100% or a large token value needs
    // more room; no font fallback and no clipping of the final digit.
    const float scale =
        std::min(1.0f, drawing.width / static_cast<float>(std::max(1, natural_width)));
    const int drawn_width = static_cast<int>(natural_width * scale);
    const int origin = drawing.centered ? (drawing.width - drawn_width) / 2 : 0;
    std::fill(drawing.pixels, drawing.pixels + drawing.stride * drawing.height, Rgb565(background));
    unsigned advance = 0;
    uint16_t palette[16];
    for (unsigned i = 0; i < 16; ++i)
        palette[i] = Rgb565(Mix(drawing.color, i * 17));
    for (unsigned char c : drawing.text) {
        if (c < 32 || c > 126)
            continue;
        const auto& glyph = mask.glyphs[c - 31];
        const int gx = static_cast<int>((advance / 16.0f + glyph.x) * scale) + origin;
        const int gy =
            static_cast<int>((mask.height - mask.baseline - glyph.height - glyph.y) * scale);
        const int w = static_cast<int>(glyph.width * scale + 0.5f);
        const int h = static_cast<int>(glyph.height * scale + 0.5f);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const int px = gx + x, py = gy + y;
                if (px < 0 || px >= drawing.width || py < 0 || py >= drawing.height)
                    continue;
                const unsigned source_x = std::min<unsigned>(glyph.width - 1, x / scale);
                const unsigned source_y = std::min<unsigned>(glyph.height - 1, y / scale);
                const unsigned index = source_y * glyph.width + source_x;
                const uint8_t packed = mask.pixels[glyph.offset + index / 2];
                const unsigned alpha = index % 2 ? packed & 15 : packed >> 4;
                if (alpha)
                    drawing.pixels[py * drawing.stride + px] = palette[alpha];
            }
        advance += glyph.advance;
    }
    lv_obj_invalidate(object);
}
}  // namespace

lv_obj_t* CodexDrawText(lv_obj_t* parent, const char* text, int x, int y, int width, uint32_t color,
                        int size, bool centered) {
    auto* object = lv_canvas_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(object, x, y);
    auto* drawing = new (std::nothrow) Drawing;
    if (drawing == nullptr)
        return object;
    drawing->mask = size == 48 ? &mask_48 : size == 20 ? &mask_20 : &mask_16;
    drawing->width = std::max(1, width);
    drawing->height = drawing->mask->height;
    drawing->color = color;
    drawing->centered = centered;
    drawing->stride =
        lv_draw_buf_width_to_stride(drawing->width, LV_COLOR_FORMAT_RGB565) / sizeof(uint16_t);
    const size_t bytes = drawing->stride * drawing->height * sizeof(uint16_t);
    drawing->pixels =
        static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (drawing->pixels == nullptr)
        drawing->pixels = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
    if (drawing->pixels == nullptr) {
        delete drawing;
        return object;
    }
    lv_obj_set_user_data(object, drawing);
    lv_canvas_set_buffer(object, drawing->pixels, drawing->width, drawing->height,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_add_event_cb(
        object,
        [](lv_event_t* event) {
            auto* data = static_cast<Drawing*>(lv_event_get_user_data(event));
            heap_caps_free(data->pixels);
            delete data;
        },
        LV_EVENT_DELETE, drawing);
    CodexDrawSetText(object, text);
    return object;
}

void CodexDrawSetText(lv_obj_t* object, const char* text) {
    if (object == nullptr || text == nullptr)
        return;
    auto* drawing = static_cast<Drawing*>(lv_obj_get_user_data(object));
    if (drawing == nullptr || drawing->text == text)
        return;
    drawing->text = text;
    Render(object, *drawing);
}

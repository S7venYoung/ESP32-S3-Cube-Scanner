#include "macintosh_theme.h"
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>
#include <new>

namespace {
// Hand-drawn masks, expanded horizontally to two-pixel stems with
// two blank columns between characters. No system-font fallback.
constexpr uint8_t digits[][5] = {
 {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},
 {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
 {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1e}};
constexpr uint8_t letters[][5] = {
 {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
 {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
 {0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},{0,0x41,0x7f,0x41,0},
 {0x20,0x40,0x41,0x3f,1},{0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
 {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
 {0x7f,9,9,9,6},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
 {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},
 {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
 {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43}};
struct PixelText { std::string text; uint16_t* pixels; int width, stride, scale; bool centered; bool inverted = false; };
uint16_t Color(uint32_t rgb) { return ((rgb >> 19) & 31) << 11 | ((rgb >> 10) & 63) << 5 | ((rgb >> 3) & 31); }
void Glyph(unsigned char c, uint8_t* out) {
    std::fill(out, out+7, 0);
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= '0' && c <= '9') std::copy(digits[c-'0'], digits[c-'0']+5, out);
    else if (c >= 'A' && c <= 'Z') std::copy(letters[c-'A'], letters[c-'A']+5, out);
    else if (c == '-') std::fill(out, out+5, 8);
    else if (c == '%') { const uint8_t v[] = {0x63,0x13,8,0x64,0x63}; std::copy(v,v+5,out); }
    else if (c == ':') { out[2] = 0x36; }
    else if (c == '.') { out[2] = 0x60; }
    else if (c == '+') { out[1]=8;out[2]=0x1c;out[3]=8; }
    else if (c == '/') { const uint8_t v[] = {0x40,0x20,0x10,8,4}; std::copy(v,v+5,out); }
    // Internal ASCII keys for Mac Control, Option, Command and Shift symbols.
    else if (c == '^') { const uint8_t v[] = {0x10,8,4,2,4,8,0x10}; std::copy(v,v+7,out); }
    else if (c == '~') { const uint8_t v[] = {2,2,4,8,0x10,0x22,0x22}; std::copy(v,v+7,out); }
    else if (c == '@') { const uint8_t v[] = {0x77,0x55,0x7f,0x14,0x7f,0x55,0x77}; std::copy(v,v+7,out); }
    else if (c == '#') { const uint8_t v[] = {8,0x0c,0x7a,0x41,0x7a,0x0c,8}; std::copy(v,v+7,out); }
}
} // namespace

lv_obj_t* MacintoshText(lv_obj_t* parent, const char* text, int x, int y, int width, int scale, bool centered) {
    auto* obj = lv_canvas_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj,x,y);
    scale = std::clamp(scale,1,4);
    const int stride = lv_draw_buf_width_to_stride(width,LV_COLOR_FORMAT_RGB565)/2;
    auto* pixels = static_cast<uint16_t*>(heap_caps_malloc(stride*7*scale*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (!pixels) pixels=static_cast<uint16_t*>(heap_caps_malloc(stride*7*scale*2,MALLOC_CAP_8BIT));
    if (!pixels) { lv_obj_delete(obj); return nullptr; }
    auto* drawing = new (std::nothrow) PixelText{"",pixels,width,stride,scale,centered};
    if (!drawing) { heap_caps_free(pixels); lv_obj_delete(obj); return nullptr; }
    lv_obj_set_user_data(obj,drawing);
    lv_canvas_set_buffer(obj,pixels,width,7*scale,LV_COLOR_FORMAT_RGB565);
    lv_obj_add_event_cb(obj,[](lv_event_t* e) {
        auto* p=static_cast<PixelText*>(lv_event_get_user_data(e));heap_caps_free(p->pixels);delete p;
    },LV_EVENT_DELETE,drawing);
    MacintoshSetText(obj,text);
    return obj;
}
void MacintoshSetText(lv_obj_t* obj,const char* text) {
    if (!obj || !text) return;
    auto* p=static_cast<PixelText*>(lv_obj_get_user_data(obj));
    if (!p || p->text==text) return;
    p->text=text;
    std::fill(p->pixels,p->pixels+p->stride*7*p->scale,Color(p->inverted ? kMacInk : kMacPaper));
    const bool symbol=p->text.size()==1 && std::strchr("^~@#",p->text[0]);
    const int cells=p->text.empty()?0:static_cast<int>(p->text.size()*8-(symbol?1:2));
    const int scale=std::max(1,std::min(p->scale,p->width/std::max(1,cells)));
    int origin=p->centered ? std::max(0,(p->width-cells*scale)/2):0;
    for (unsigned char c:p->text) {
        uint8_t glyph[7];Glyph(c,glyph);
        for (int x=0;x<(symbol?7:5);++x) for(int y=0;y<7;++y) if (glyph[x]&(1<<y))
            for(int dx=0;dx<(symbol?1:2)*scale;++dx) for(int dy=0;dy<scale;++dy) {
                const int px=origin+x*scale+dx, py=y*scale+dy;
                if(px<p->width) p->pixels[py*p->stride+px]=Color(p->inverted ? kMacPaper : kMacInk);
            }
        origin+=8*scale;
    }
    lv_obj_invalidate(obj);
}

void MacintoshSetInverted(lv_obj_t* obj, bool inverted) {
    if (!obj) return;
    auto* p=static_cast<PixelText*>(lv_obj_get_user_data(obj));
    if (!p || p->inverted==inverted) return;
    p->inverted=inverted;
    const std::string text=p->text;
    p->text.clear();
    MacintoshSetText(obj,text.c_str());
}

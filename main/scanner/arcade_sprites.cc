#include "arcade_sprites.h"
#include <esp_heap_caps.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include "arcade_assets/arcade_dojo.inc"
#include "arcade_assets/arcade_title.inc"
#include "arcade_assets/arcade_versus.inc"
#include "arcade_assets/arcade_today.inc"
#include "arcade_assets/arcade_frame_0_0.inc"
#include "arcade_assets/arcade_frame_0_1.inc"
#include "arcade_assets/arcade_frame_0_2.inc"
#include "arcade_assets/arcade_frame_0_3.inc"
#include "arcade_assets/arcade_frame_0_4.inc"
#include "arcade_assets/arcade_frame_0_5.inc"
#include "arcade_assets/arcade_frame_0_6.inc"
#include "arcade_assets/arcade_frame_0_7.inc"
#include "arcade_assets/arcade_frame_0_8.inc"
#include "arcade_assets/arcade_frame_1_0.inc"
#include "arcade_assets/arcade_frame_1_1.inc"
#include "arcade_assets/arcade_frame_1_2.inc"
#include "arcade_assets/arcade_frame_1_3.inc"
#include "arcade_assets/arcade_frame_1_4.inc"
#include "arcade_assets/arcade_frame_1_5.inc"
#include "arcade_assets/arcade_frame_1_6.inc"
#include "arcade_assets/arcade_frame_1_7.inc"
#include "arcade_assets/arcade_frame_1_8.inc"
namespace {
const lv_image_dsc_t* DojoImage() {
    static const lv_image_dsc_t image = [] {
        lv_image_dsc_t d{};
        d.header.magic = LV_IMAGE_HEADER_MAGIC;
        d.header.cf = LV_COLOR_FORMAT_RGB565;
        d.header.w = 236;
        d.header.h = 132;
        d.header.stride = 236 * 2;
        d.data_size = sizeof(dojo_pixels);
        d.data = dojo_pixels;
        return d;
    }();
    return &image;
}
struct Frame {const uint32_t* palette;const uint8_t* runs;size_t length;};
#define FRAME(s,p) {arcade_frame_##s##_##p##_palette,arcade_frame_##s##_##p##_runs,sizeof(arcade_frame_##s##_##p##_runs)}
constexpr Frame frames[2][9]={{FRAME(0,0),FRAME(0,1),FRAME(0,2),FRAME(0,3),FRAME(0,4),FRAME(0,5),FRAME(0,6),FRAME(0,7),FRAME(0,8)},
                            {FRAME(1,0),FRAME(1,1),FRAME(1,2),FRAME(1,3),FRAME(1,4),FRAME(1,5),FRAME(1,6),FRAME(1,7),FRAME(1,8)}};
#undef FRAME
}
lv_obj_t* ArcadeDojoCreate(lv_obj_t* parent) {
    auto* image=lv_image_create(parent);
    lv_image_set_src(image,DojoImage());
    lv_obj_set_pos(image,2,58);
    lv_obj_remove_flag(image,LV_OBJ_FLAG_CLICKABLE);
    return image;
}
lv_obj_t* ArcadeArtworkCreate(lv_obj_t* parent, int asset, int x, int y) {
    static const auto images = [] {
        std::array<lv_image_dsc_t, 3> result{};
        const uint8_t* pixels[] = {arcade_title_pixels, arcade_versus_pixels, arcade_today_pixels};
        const unsigned widths[] = {140, 61, 158};
        const unsigned heights[] = {25, 78, 43};
        for (int i = 0; i < 3; ++i) {
            result[i].header.magic = LV_IMAGE_HEADER_MAGIC;
            result[i].header.cf = LV_COLOR_FORMAT_ARGB8888;
            result[i].header.w = widths[i];
            result[i].header.h = heights[i];
            result[i].header.stride = widths[i] * 4;
            result[i].data_size = widths[i] * heights[i] * 4;
            result[i].data = pixels[i];
        }
        return result;
    }();
    if (asset < 0 || asset > 2) return nullptr;
    auto* image = lv_image_create(parent);
    lv_image_set_src(image, &images[asset]);
    lv_obj_set_pos(image, x, y);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    return image;
}
lv_obj_t* ArcadeSpriteCreate(lv_obj_t* parent) {
    auto* pixels=static_cast<uint8_t*>(heap_caps_calloc(96*104,4,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!pixels)pixels=static_cast<uint8_t*>(heap_caps_calloc(96*104,4,MALLOC_CAP_8BIT));
    if(!pixels)return nullptr;
    auto* canvas=lv_canvas_create(parent);lv_obj_remove_style_all(canvas);
    lv_canvas_set_buffer(canvas,pixels,96,104,LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_user_data(canvas,pixels);lv_obj_remove_flag(canvas,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(canvas,[](lv_event_t* e){heap_caps_free(lv_event_get_user_data(e));},LV_EVENT_DELETE,pixels);
    return canvas;
}
void ArcadeSpriteSet(lv_obj_t* canvas,int side,int pose) {
    if(!canvas || side<0 || side>1 || pose<0 || pose>8)return;
    const auto& f=frames[side][pose];auto* out=static_cast<uint8_t*>(lv_obj_get_user_data(canvas));size_t at=0;
    for(size_t i=0;i+1<f.length;i+=2){
        const uint32_t color=f.palette[f.runs[i+1]];
        for(unsigned n=0;n<f.runs[i] && at<96*104;++n,++at){
            out[at*4]=color;out[at*4+1]=color>>8;out[at*4+2]=color>>16;out[at*4+3]=color>>24;
        }
    }
    lv_obj_invalidate(canvas);
}

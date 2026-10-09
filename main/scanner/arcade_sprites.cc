#include "arcade_sprites.h"
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstdint>
#include "arcade_assets/arcade_frame_0_0.inc"
#include "arcade_assets/arcade_frame_0_1.inc"
#include "arcade_assets/arcade_frame_0_2.inc"
#include "arcade_assets/arcade_frame_0_3.inc"
#include "arcade_assets/arcade_frame_0_4.inc"
#include "arcade_assets/arcade_frame_0_5.inc"
#include "arcade_assets/arcade_frame_1_0.inc"
#include "arcade_assets/arcade_frame_1_1.inc"
#include "arcade_assets/arcade_frame_1_2.inc"
#include "arcade_assets/arcade_frame_1_3.inc"
#include "arcade_assets/arcade_frame_1_4.inc"
#include "arcade_assets/arcade_frame_1_5.inc"
namespace {
struct Frame {const uint32_t* palette;const uint8_t* runs;size_t length;};
#define FRAME(s,p) {arcade_frame_##s##_##p##_palette,arcade_frame_##s##_##p##_runs,sizeof(arcade_frame_##s##_##p##_runs)}
constexpr Frame frames[2][6]={{FRAME(0,0),FRAME(0,1),FRAME(0,2),FRAME(0,3),FRAME(0,4),FRAME(0,5)},
                            {FRAME(1,0),FRAME(1,1),FRAME(1,2),FRAME(1,3),FRAME(1,4),FRAME(1,5)}};
#undef FRAME
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
    if(!canvas || side<0 || side>1 || pose<0 || pose>5)return;
    const auto& f=frames[side][pose];auto* out=static_cast<uint8_t*>(lv_obj_get_user_data(canvas));size_t at=0;
    for(size_t i=0;i+1<f.length;i+=2){
        const uint32_t color=f.palette[f.runs[i+1]];
        for(unsigned n=0;n<f.runs[i] && at<96*104;++n,++at){
            out[at*4]=color;out[at*4+1]=color>>8;out[at*4+2]=color>>16;out[at*4+3]=color>>24;
        }
    }
    lv_obj_invalidate(canvas);
}

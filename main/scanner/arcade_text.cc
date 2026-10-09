#include "arcade_text.h"
#include "arcade_lettering.h"
#include <esp_heap_caps.h>
#include <cstring>
#include <new>
#include <string>
#include <cstdio>
#include <cstdlib>

namespace {
struct Drawing {std::string text;uint8_t* pixels;int width,height,stride;uint32_t color;};
void Render(lv_obj_t* object,Drawing& d) {
    if(!d.text.empty() && d.text[0]=='$')
        ArcadeControls::Energy(d.pixels,d.width,d.height,d.stride,std::atoi(d.text.c_str()+1),d.text.find('R')!=std::string::npos);
    else ArcadeLettering::Paint(d.pixels,d.width,d.height,d.stride,d.text.c_str(),d.color);
    lv_obj_invalidate(object);
}
}
lv_obj_t* ArcadeText(lv_obj_t* parent,const char* text,int x,int y,int width,uint32_t color,int height) {
    width=std::max(8,width);height=std::clamp(height,12,48);
    const int stride=lv_draw_buf_width_to_stride(width,LV_COLOR_FORMAT_ARGB8888);
    auto* pixels=static_cast<uint8_t*>(heap_caps_calloc(stride,height,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!pixels)pixels=static_cast<uint8_t*>(heap_caps_calloc(stride,height,MALLOC_CAP_8BIT));
    if(!pixels)return nullptr;
    auto* d=new(std::nothrow) Drawing{"",pixels,width,height,stride,color};
    if(!d){heap_caps_free(pixels);return nullptr;}
    auto* obj=lv_canvas_create(parent);lv_obj_remove_style_all(obj);lv_obj_set_pos(obj,x,y);
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(obj,d);lv_canvas_set_buffer(obj,pixels,width,height,LV_COLOR_FORMAT_ARGB8888);
    lv_obj_add_event_cb(obj,[](lv_event_t* e){auto* p=static_cast<Drawing*>(lv_event_get_user_data(e));heap_caps_free(p->pixels);delete p;},LV_EVENT_DELETE,d);
    ArcadeTextSet(obj,text);return obj;
}
void ArcadeTextSet(lv_obj_t* obj,const char* text){
    if(!obj || !text)return;auto* d=static_cast<Drawing*>(lv_obj_get_user_data(obj));
    if(d->text==text)return;d->text=text;Render(obj,*d);
}
void ArcadeTextColor(lv_obj_t* obj,uint32_t color){
    if(!obj)return;auto* d=static_cast<Drawing*>(lv_obj_get_user_data(obj));
    if(d->color==color)return;d->color=color;Render(obj,*d);
}
lv_obj_t* ArcadeEnergy(lv_obj_t* parent,int x,int y,int width,int height) {
    return ArcadeText(parent,"$-1",x,y,width,0,height);
}
void ArcadeEnergySet(lv_obj_t* object,int percent,bool right_aligned) {
    char value[12];std::snprintf(value,sizeof(value),"$%d%c",std::clamp(percent,-1,100),right_aligned?'R':'L');
    ArcadeTextSet(object,value);
}

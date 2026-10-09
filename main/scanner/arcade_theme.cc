#include "arcade_theme.h"
#include "arcade_motion.h"
#include "arcade_sprites.h"
#include "arcade_text.h"
#include "codex_metrics.h"
#include "fight_telemetry.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

namespace {
constexpr uint32_t ink = 0x101411, white = 0xF3EEE5, red = 0xFF442C, gold = 0xFFBF18, blue = 0x21CEEE;
struct Scene {
    lv_obj_t *root, *quota[2], *health[2], *battery[2], *rage[2], *fighter[2], *effect[2];
    lv_obj_t *total, *transport, *cube, *cube_fill, *keys[4], *symbols[4];
    lv_timer_t* timer = nullptr;
    ArcadeMotion motion[2];
    int shown_pose[2] = {-1,-1};
};
lv_obj_t* Box(lv_obj_t* p,int x,int y,int w,int h,uint32_t color,int radius=0) {
    auto* o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_radius(o,radius,0);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
lv_obj_t* Text(lv_obj_t* p,const char* s,int x,int y,int w,uint32_t color,int size=16) {
    return ArcadeText(p,s,x,y,w,color,size);
}
lv_obj_t* StatusLabel(lv_obj_t* parent,const char* text,int x,int width,uint32_t color) {
    auto* label=lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_obj_set_pos(label,x,5);lv_obj_set_width(label,width);
    lv_obj_set_style_text_font(label,LV_FONT_DEFAULT,0);
    lv_obj_set_style_text_color(label,lv_color_hex(color),0);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(label,LV_LABEL_LONG_CLIP);
    lv_label_set_text(label,text);
    return label;
}
void Bar(lv_obj_t* bar,int value,int width) {
    if(value<0 || value==0) lv_obj_add_flag(bar,LV_OBJ_FLAG_HIDDEN);
    else {lv_obj_set_width(bar,std::max(1,width*std::clamp(value,0,100)/100));lv_obj_remove_flag(bar,LV_OBJ_FLAG_HIDDEN);}
}
void Tick(Scene& s) {
    if(lv_obj_has_flag(s.root,LV_OBJ_FLAG_HIDDEN)) {
        for(auto& motion:s.motion) motion.Reset(lv_tick_get());
        return;
    }
    const auto f=GetFightTelemetry();
    const int speeds[]={f.left_wpm,f.right_wpm};
    for(int i=0;i<2;++i) {
        auto& m=s.motion[i];m.Step(lv_tick_get(),speeds[i],f.online);
        const int idle_frame=ArcadeIdleFrame(lv_tick_get(),i);
        const int frame=m.pose==0?6+idle_frame:m.pose;
        if(frame!=s.shown_pose[i]) {ArcadeSpriteSet(s.fighter[i],i,frame);s.shown_pose[i]=frame;}
        const bool energy=m.pose==3 || m.pose==5;
        if(energy) {
            lv_obj_remove_flag(s.effect[i],LV_OBJ_FLAG_HIDDEN);
            const int travel=std::min<int>(70,m.elapsed/5);
            lv_obj_set_pos(s.effect[i],i==0?74+travel:150-travel, m.pose==5?102:120);
            lv_obj_set_size(s.effect[i],m.pose==5?36:16,m.pose==5?25:12);
        } else lv_obj_add_flag(s.effect[i],LV_OBJ_FLAG_HIDDEN);
        if(s.fighter[i])lv_obj_set_y(s.fighter[i],m.pose==4?-9:0);
    }
}
} // namespace

lv_obj_t* ArcadeCreate(lv_obj_t* parent) {
    auto* s=new(std::nothrow) Scene{};if(!s)return nullptr;
    s->root=Box(parent,0,0,240,240,ink);
    lv_obj_set_user_data(s->root,s);
    lv_obj_set_style_border_color(s->root,lv_color_hex(red),0);lv_obj_set_style_border_width(s->root,1,0);
    ArcadeArtworkCreate(s->root,0,5,1);
    s->transport=StatusLabel(s->root,"OFF",151,34,white);
    auto* cube_icon=Box(s->root,186,7,16,10,0x131713,2);
    lv_obj_set_style_border_color(cube_icon,lv_color_hex(white),0);
    lv_obj_set_style_border_width(cube_icon,1,0);
    Box(s->root,202,10,2,4,white,1);
    s->cube_fill=Box(cube_icon,2,2,12,6,0x25CF60,1);
    s->cube=StatusLabel(s->root,"--%",205,34,white);
    for(int i=0;i<2;++i) {
        const int x=i==0?6:126;
        auto* frame=Box(s->root,x,27,108,11,0x191918,3);
        lv_obj_set_style_border_color(frame,lv_color_hex(white),0);
        lv_obj_set_style_border_width(frame,1,0);
        s->health[i]=Box(frame,2,2,104,7,i==0?red:gold,2);
        lv_obj_set_style_bg_grad_color(s->health[i],lv_color_hex(i==0?gold:red),0);
        lv_obj_set_style_bg_grad_dir(s->health[i],LV_GRAD_DIR_HOR,0);
        Text(s->root,i==0?"5H LEFT":"7D LEFT",i==0?9:166,40,i==0?46:39,white,12);
        s->quota[i]=Text(s->root,"--%",i==0?85:211,40,25,gold,12);
    }
    ArcadeDojoCreate(s->root);
    // Stage is a fixed opaque image, only fighters/effects animate above it.
    auto* stage=lv_obj_create(s->root);lv_obj_remove_style_all(stage);
    lv_obj_set_pos(stage,2,57);lv_obj_set_size(stage,236,120);
    lv_obj_remove_flag(stage,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(stage,LV_OBJ_FLAG_CLICKABLE);
    for(int i=0;i<2;++i) {
        s->fighter[i]=ArcadeSpriteCreate(stage);if(s->fighter[i]) {
            lv_obj_set_pos(s->fighter[i],i==0?-12:109,0);
            lv_image_set_pivot(s->fighter[i],0,0);lv_image_set_scale(s->fighter[i],352);
        }
        s->effect[i]=Box(s->root,100,120,16,12,i==0?blue:gold,20);
        lv_obj_set_style_shadow_color(s->effect[i],lv_color_hex(i==0?blue:red),0);
        lv_obj_set_style_shadow_width(s->effect[i],8,0);lv_obj_add_flag(s->effect[i],LV_OBJ_FLAG_HIDDEN);
    }
    ArcadeArtworkCreate(s->root,1,91,75);
    ArcadeArtworkCreate(s->root,2,41,140);
    s->total=Text(s->root,"--",68,156,104,red,28);
    for(int i=0;i<2;++i) {
        const int x=i==0?6:126;
        s->rage[i]=ArcadeEnergy(s->root,x,177,108,25);
        Text(s->root,i==0?"L BAT":"R BAT",x+10,178,34,white,12);
        s->battery[i]=Text(s->root,"--%",x+45,178,43,blue,12);
    }
    for(int i=0;i<4;++i) {
        s->keys[i]=Box(s->root,27+i*44,207,40,25,0x181818,4);
        lv_obj_set_style_border_width(s->keys[i],1,0);
        lv_obj_set_style_border_color(s->keys[i],lv_color_hex(0x666962),0);
        const char* symbols[]={"^","~","@","#"};
        s->symbols[i]=ArcadeText(s->keys[i],symbols[i],8,1,24,white,23);
    }
    s->timer=lv_timer_create([](lv_timer_t* timer){Tick(*static_cast<Scene*>(lv_timer_get_user_data(timer)));},80,s);
    lv_obj_add_event_cb(s->root,[](lv_event_t* e){auto* s=static_cast<Scene*>(lv_event_get_user_data(e));lv_timer_delete(s->timer);delete s;},LV_EVENT_DELETE,s);
    ArcadeRefresh(s->root);return s->root;
}
void ArcadeSetBattery(lv_obj_t* root,const char* text) {
    if(!root || !text)return;
    auto& s=*static_cast<Scene*>(lv_obj_get_user_data(root));
    lv_label_set_text(s.cube,text);
    const char* number=text;while(*number && (*number<'0'||*number>'9'))++number;
    if(!*number)Bar(s.cube_fill,-1,12);
    else {int percent=0;if(std::sscanf(number,"%d",&percent)==1)Bar(s.cube_fill,percent,12);}
}
void ArcadeSetModifiers(lv_obj_t* root,uint8_t modifiers) {
    if(!root)return;auto& s=*static_cast<Scene*>(lv_obj_get_user_data(root));
    for(int i=0;i<4;++i){const bool active=modifiers&(1u<<i);lv_obj_set_style_bg_color(s.keys[i],lv_color_hex(active?red:ink),0);ArcadeTextColor(s.symbols[i],white);}
}
void ArcadeRefresh(lv_obj_t* root) {
    if(!root || lv_obj_has_flag(root,LV_OBJ_FLAG_HIDDEN))return;
    auto& s=*static_cast<Scene*>(lv_obj_get_user_data(root));const auto c=GetCodexSnapshot();const auto f=GetFightTelemetry();char text[40];
    lv_label_set_text(s.transport,std::strcmp(c.transport,"OFFLINE")==0?"OFF":c.transport);
    const int quota[]={c.online?c.metrics.left:-1,c.online?c.metrics.week_left:-1};
    const int battery[]={f.online?f.left_battery:-1,f.online?f.right_battery:-1};
    for(int i=0;i<2;++i){
        if(quota[i]>=0)std::snprintf(text,sizeof(text),"%d%%",quota[i]);else std::snprintf(text,sizeof(text),"--%%");ArcadeTextSet(s.quota[i],text);Bar(s.health[i],quota[i],104);
        if(battery[i]>=0)std::snprintf(text,sizeof(text),"%d%%",battery[i]);else std::snprintf(text,sizeof(text),"--%%");ArcadeTextSet(s.battery[i],text);ArcadeEnergySet(s.rage[i],battery[i]);
    }
    if(!c.online || c.metrics.tokens<0)std::snprintf(text,sizeof(text),"--");
    else if(c.metrics.tokens>=1000000)std::snprintf(text,sizeof(text),"%.1fM",c.metrics.tokens/1000000.0);
    else if(c.metrics.tokens>=1000)std::snprintf(text,sizeof(text),"%.1fK",c.metrics.tokens/1000.0);
    else std::snprintf(text,sizeof(text),"%lld",static_cast<long long>(c.metrics.tokens));
    ArcadeTextSet(s.total,text);ArcadeSetModifiers(root,f.online?f.modifiers:0);Tick(s);
}

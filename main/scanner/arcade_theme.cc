#include "arcade_theme.h"
#include "arcade_motion.h"
#include "arcade_sprites.h"
#include "codex_draw.h"
#include "codex_metrics.h"
#include "fight_telemetry.h"
#include "macintosh_theme.h"
#include <algorithm>
#include <cstdio>
#include <new>

namespace {
constexpr uint32_t ink = 0x101411, white = 0xF3EEE5, red = 0xFF442C, gold = 0xFFBF18, blue = 0x21CEEE;
struct Scene {
    lv_obj_t *root, *quota[2], *health[2], *battery[2], *rage[2], *wpm[2], *fighter[2], *effect[2];
    lv_obj_t *total, *transport, *cube, *keys[4], *symbols[4];
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
    return CodexDrawText(p,s,x,y,w,color,size,true);
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
        if(m.pose!=s.shown_pose[i]) {ArcadeSpriteSet(s.fighter[i],i,m.pose);s.shown_pose[i]=m.pose;}
        const bool energy=m.pose==3 || m.pose==5;
        if(energy) {
            lv_obj_remove_flag(s.effect[i],LV_OBJ_FLAG_HIDDEN);
            const int travel=std::min<int>(70,m.elapsed/5);
            lv_obj_set_pos(s.effect[i],i==0?74+travel:150-travel, m.pose==5?102:120);
            lv_obj_set_size(s.effect[i],m.pose==5?36:16,m.pose==5?25:12);
        } else lv_obj_add_flag(s.effect[i],LV_OBJ_FLAG_HIDDEN);
        if(s.fighter[i])lv_obj_set_y(s.fighter[i],m.pose==4?68:76+(m.pose==0 && f.online && speeds[i]>0 ? (lv_tick_get()/350)%2:0));
    }
}
} // namespace

lv_obj_t* ArcadeCreate(lv_obj_t* parent) {
    auto* s=new(std::nothrow) Scene{};if(!s)return nullptr;
    s->root=Box(parent,0,0,240,240,ink);
    lv_obj_set_user_data(s->root,s);
    lv_obj_set_style_border_color(s->root,lv_color_hex(red),0);lv_obj_set_style_border_width(s->root,1,0);
    Text(s->root,"CODEX FIGHT",5,3,147,red,20);
    s->transport=Text(s->root,"OFF",154,4,34,white);
    s->cube=Text(s->root,"--%",190,4,44,gold);
    for(int i=0;i<2;++i) {
        const int x=i==0?6:126;
        Box(s->root,x,28,108,8,0x39403B,3);s->health[i]=Box(s->root,x,28,108,8,i==0?red:gold,3);
        Text(s->root,i==0?"5H LEFT":"7D LEFT",x,39,64,white);
        s->quota[i]=Text(s->root,"--%",x+64,39,44,gold);
        s->wpm[i]=Text(s->root,i==0?"L --":"R --",x,58,108,white);
    }
    // Fixed dark stage rules: no full-screen flashing, no per-frame allocation.
    Box(s->root,8,177,224,2,0x48554F);Box(s->root,16,173,208,1,0x26362F);
    Text(s->root,"VS",101,93,38,red,20);
    for(int i=0;i<2;++i) {
        s->fighter[i]=ArcadeSpriteCreate(s->root);if(s->fighter[i])lv_obj_set_pos(s->fighter[i],i==0?8:136,76);
        s->effect[i]=Box(s->root,100,120,16,12,i==0?blue:gold,20);
        lv_obj_set_style_shadow_color(s->effect[i],lv_color_hex(i==0?blue:red),0);
        lv_obj_set_style_shadow_width(s->effect[i],8,0);lv_obj_add_flag(s->effect[i],LV_OBJ_FLAG_HIDDEN);
    }
    Box(s->root,58,151,124,27,ink,2);
    s->total=Text(s->root,"TODAY --",60,156,120,gold,20);
    for(int i=0;i<2;++i) {
        const int x=i==0?6:126;
        s->battery[i]=Text(s->root,i==0?"L BAT --%":"R BAT --%",x,182,108,blue);
        Box(s->root,x,201,108,8,0x34403D,2);s->rage[i]=Box(s->root,x,201,108,8,blue,2);
    }
    for(int i=0;i<4;++i) {
        s->keys[i]=Box(s->root,38+i*42,215,36,21,kMacPaper,3);
        const char* symbols[]={"^","~","@","#"};
        s->symbols[i]=MacintoshText(s->keys[i],symbols[i],4,3,28,2,true);
    }
    s->timer=lv_timer_create([](lv_timer_t* timer){Tick(*static_cast<Scene*>(lv_timer_get_user_data(timer)));},80,s);
    lv_obj_add_event_cb(s->root,[](lv_event_t* e){auto* s=static_cast<Scene*>(lv_event_get_user_data(e));lv_timer_delete(s->timer);delete s;},LV_EVENT_DELETE,s);
    ArcadeRefresh(s->root);return s->root;
}
void ArcadeSetBattery(lv_obj_t* root,const char* text) {
    if(root)CodexDrawSetText(static_cast<Scene*>(lv_obj_get_user_data(root))->cube,text);
}
void ArcadeSetModifiers(lv_obj_t* root,uint8_t modifiers) {
    if(!root)return;auto& s=*static_cast<Scene*>(lv_obj_get_user_data(root));
    for(int i=0;i<4;++i){const bool active=modifiers&(1u<<i);lv_obj_set_style_bg_color(s.keys[i],lv_color_hex(active?kMacInk:kMacPaper),0);MacintoshSetInverted(s.symbols[i],active);}
}
void ArcadeRefresh(lv_obj_t* root) {
    if(!root || lv_obj_has_flag(root,LV_OBJ_FLAG_HIDDEN))return;
    auto& s=*static_cast<Scene*>(lv_obj_get_user_data(root));const auto c=GetCodexSnapshot();const auto f=GetFightTelemetry();char text[40];
    CodexDrawSetText(s.transport,c.transport);
    const int quota[]={c.online?c.metrics.left:-1,c.online?c.metrics.week_left:-1};
    const int battery[]={f.online?f.left_battery:-1,f.online?f.right_battery:-1};
    const int speed[]={f.online?f.left_wpm:-1,f.online?f.right_wpm:-1};
    for(int i=0;i<2;++i){
        if(quota[i]>=0)std::snprintf(text,sizeof(text),"%d%%",quota[i]);else std::snprintf(text,sizeof(text),"--%%");CodexDrawSetText(s.quota[i],text);Bar(s.health[i],quota[i],108);
        if(battery[i]>=0)std::snprintf(text,sizeof(text),"%c BAT %d%%",i?'R':'L',battery[i]);else std::snprintf(text,sizeof(text),"%c BAT --%%",i?'R':'L');CodexDrawSetText(s.battery[i],text);Bar(s.rage[i],battery[i],108);
        if(speed[i]>=0)std::snprintf(text,sizeof(text),"%c WPM %d",i?'R':'L',speed[i]);else std::snprintf(text,sizeof(text),"%c WPM --",i?'R':'L');CodexDrawSetText(s.wpm[i],text);
    }
    if(!c.online || c.metrics.tokens<0)std::snprintf(text,sizeof(text),"TODAY --");
    else if(c.metrics.tokens>=1000000)std::snprintf(text,sizeof(text),"TODAY %.1fM",c.metrics.tokens/1000000.0);
    else if(c.metrics.tokens>=1000)std::snprintf(text,sizeof(text),"TODAY %.1fK",c.metrics.tokens/1000.0);
    else std::snprintf(text,sizeof(text),"TODAY %lld",static_cast<long long>(c.metrics.tokens));
    CodexDrawSetText(s.total,text);ArcadeSetModifiers(root,f.online?f.modifiers:0);Tick(s);
}

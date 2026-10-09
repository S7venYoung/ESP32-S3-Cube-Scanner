#pragma once
#include <lvgl.h>
lv_obj_t* ArcadeText(lv_obj_t* parent,const char* text,int x,int y,int width,uint32_t color,int height=16);
void ArcadeTextSet(lv_obj_t* object,const char* text);
void ArcadeTextColor(lv_obj_t* object,uint32_t color);
lv_obj_t* ArcadeEnergy(lv_obj_t* parent,int x,int y,int width,int height);
void ArcadeEnergySet(lv_obj_t* object,int percent);

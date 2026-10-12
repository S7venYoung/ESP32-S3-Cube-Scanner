#pragma once
#include <lvgl.h>
lv_obj_t* WhiteCreate(lv_obj_t* parent);
void WhiteRefresh(lv_obj_t* root);
void WhiteSetModifiers(lv_obj_t* root, uint8_t modifiers);
void WhiteSetBattery(lv_obj_t* root, int percent);

#pragma once
#include <lvgl.h>
lv_obj_t* ArcadeCreate(lv_obj_t* parent);
void ArcadeSetBattery(lv_obj_t* root, const char* text);
void ArcadeSetModifiers(lv_obj_t* root, uint8_t modifiers);
void ArcadeRefresh(lv_obj_t* root);

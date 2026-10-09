#pragma once
#include <lvgl.h>
// One persistent canvas per fighter, reused for every authored pose.
lv_obj_t* ArcadeSpriteCreate(lv_obj_t* parent);
void ArcadeSpriteSet(lv_obj_t* canvas, int side, int pose);
lv_obj_t* ArcadeDojoCreate(lv_obj_t* parent);

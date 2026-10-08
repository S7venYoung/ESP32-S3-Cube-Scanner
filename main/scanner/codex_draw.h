#pragma once
#include <lvgl.h>
lv_obj_t* CodexDrawText(lv_obj_t* parent, const char* text, int x, int y, int width, uint32_t color,
                        int size = 16, bool centered = false);
void CodexDrawSetText(lv_obj_t* object, const char* text);

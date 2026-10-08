#pragma once
#include <lvgl.h>
#include <string>

lv_obj_t* MacintoshText(lv_obj_t* parent, const char* text, int x, int y, int width,
                        int scale = 1, bool centered = false);
void MacintoshSetText(lv_obj_t* object, const char* text);
constexpr uint32_t kMacPaper = 0xD8D1BE;
constexpr uint32_t kMacInk = 0x282922;

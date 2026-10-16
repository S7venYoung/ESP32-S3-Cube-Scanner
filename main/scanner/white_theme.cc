#include "white_theme.h"
#include <esp_timer.h>
#include <algorithm>
#include <cstdio>
#include <new>
#include "fight_telemetry.h"
#include "keyboard_status.h"
#include "modifier_timeline.h"

namespace {
constexpr uint32_t paper = 0xF8F8F8, ink = 0x202020, muted = 0x808080;
constexpr uint32_t rule = 0xDDDDDD, blue = 0x167AC6, active = 0xDBEAFC;
struct Battery {
    lv_obj_t *fill, *text;
    int width;
};
struct View {
    lv_obj_t *layer, *layer_bold, *transport, *profile, *wpm, *wpm_bold, *keys[4], *key_text[4],
        *bars[9];
    Battery own, left, right;
    int history[9] = {};
    int64_t last_history = -1;
};
lv_obj_t* Box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, int radius = 0) {
    auto* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o,
                       static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return o;
}
lv_obj_t* Text(lv_obj_t* parent, const char* value, int x, int y, int w, int height,
               uint32_t color = ink, bool center = false) {
    auto* o = lv_label_create(parent);
    lv_obj_set_style_text_font(o, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
    lv_label_set_text(o, value);
    // Reuse the existing system font, scaled to the reference's visual hierarchy.
    const int scale = height * 256 / (LV_FONT_DEFAULT)->line_height;
    lv_obj_set_width(o, w * 256 / std::max(1, scale));
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_transform_pivot_x(o, 0, 0);
    lv_obj_set_style_transform_pivot_y(o, 0, 0);
    lv_obj_set_style_transform_scale_x(o, scale, 0);
    lv_obj_set_style_transform_scale_y(o, scale, 0);
    if (center)
        lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
    return o;
}
Battery MakeBattery(lv_obj_t* root, int x, int y, int w, int h, int tx, int ty, int tw, int th) {
    Box(root, x, y, w, h, ink, 3);
    Box(root, x + w, y + h / 3, 3, h / 3, ink, 1);
    Box(root, x + 2, y + 2, w - 4, h - 4, paper, 2);
    auto* fill = Box(root, x + 4, y + 4, w - 8, h - 8, 0x36AE58, 1);
    lv_obj_add_flag(fill, LV_OBJ_FLAG_HIDDEN);
    return {fill, Text(root, "--%", tx, ty, tw, th, ink, true), w - 8};
}
void SetBattery(Battery& battery, int value) {
    char text[12] = "--%";
    if (value >= 0 && value <= 100)
        std::snprintf(text, sizeof(text), "%d%%", value);
    lv_label_set_text(battery.text, text);
    if (value <= 0 || value > 100)
        lv_obj_add_flag(battery.fill, LV_OBJ_FLAG_HIDDEN);
    else {
        lv_obj_remove_flag(battery.fill, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(battery.fill, std::max(1, value * battery.width / 100));
        lv_obj_set_style_bg_color(battery.fill, lv_color_hex(value <= 20 ? 0xE48B32 : 0x36AE58), 0);
    }
}
View* Get(lv_obj_t* root) {
    return root ? static_cast<View*>(lv_obj_get_user_data(root)) : nullptr;
}
}  // namespace

lv_obj_t* WhiteCreate(lv_obj_t* parent) {
    auto* v = new (std::nothrow) View{};
    if (!v)
        return nullptr;
    auto* root = Box(parent, 0, 0, 240, 240, paper);
    lv_obj_set_user_data(root, v);
    lv_obj_add_event_cb(
        root, [](lv_event_t* e) { delete static_cast<View*>(lv_event_get_user_data(e)); },
        LV_EVENT_DELETE, v);
    Text(root, "ZMK", 11, 19, 43, 16, muted);
    Box(root, 58, 14, 1, 29, rule);
    // Bluetooth rune, drawn with line primitives instead of a font glyph.
    static const lv_point_precise_t rune[] = {{0, 4}, {10, 14}, {5, 19}, {5, 0}, {10, 5}, {0, 15}};
    auto* bt = lv_line_create(root);
    lv_line_set_points(bt, rune, 6);
    lv_obj_set_pos(bt, 67, 18);
    lv_obj_set_style_line_width(bt, 2, 0);
    lv_obj_set_style_line_color(bt, lv_color_hex(blue), 0);
    v->transport = Text(root, "--", 84, 19, 33, 16);
    auto* pill = Box(root, 119, 17, 26, 23, active, 7);
    v->profile = Text(pill, "--", 0, 3, 26, 17, blue, true);
    Box(root, 151, 14, 1, 29, rule);
    v->own = MakeBattery(root, 163, 21, 28, 15, 196, 18, 37, 19);
    Text(root, "LAYER", 12, 70, 93, 12, muted);
    v->layer_bold = Text(root, "--", 11, 92, 106, 36);
    v->layer = Text(root, "--", 10, 92, 106, 36);
    Box(root, 117, 74, 1, 65, rule);
    const char* names[] = {"Ctrl", "Alt", "Gui", "Sft"};
    for (int i = 0; i < 4; ++i) {
        v->keys[i] = Box(root, 125 + i * 27, 91, 24, 35, 0xEEEEEE, 5);
        v->key_text[i] = Text(v->keys[i], names[i], 0, 11, 24, 12, muted, true);
    }
    Box(root, 11, 154, 218, 1, rule);
    Box(root, 80, 166, 1, 66, rule);
    Box(root, 160, 166, 1, 66, rule);
    Text(root, "LEFT KB", 3, 164, 74, 11, muted, true);
    Text(root, "WPM", 83, 164, 74, 11, muted, true);
    Text(root, "RIGHT KB", 163, 164, 74, 11, muted, true);
    v->left = MakeBattery(root, 24, 187, 33, 18, 7, 211, 66, 23);
    v->right = MakeBattery(root, 184, 187, 33, 18, 166, 211, 70, 23);
    v->wpm_bold = Text(root, "--", 85, 184, 73, 35, ink, true);
    v->wpm = Text(root, "--", 84, 184, 73, 35, ink, true);
    for (int i = 0; i < 9; ++i)
        v->bars[i] = Box(root, 92 + i * 6, 232, 3, 1, 0xCCCCCC, 2);
    return root;
}
void WhiteSetModifiers(lv_obj_t* root, uint8_t modifiers) {
    auto* v = Get(root);
    if (!v)
        return;
    for (int i = 0; i < 4; ++i) {
        const bool pressed = modifiers & (1u << i);
        lv_obj_set_style_bg_color(v->keys[i], lv_color_hex(pressed ? active : 0xEEEEEE), 0);
        lv_obj_set_style_text_color(v->key_text[i], lv_color_hex(pressed ? blue : muted), 0);
    }
}
void WhiteSetBattery(lv_obj_t* root, int percent) {
    auto* v = Get(root);
    if (v)
        SetBattery(v->own, percent);
}
void WhiteRefresh(lv_obj_t* root) {
    auto* v = Get(root);
    if (!v || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN))
        return;
    const auto kb = GetKeyboardStatus();
    const auto fight = GetFightTelemetry();
    WhiteSetModifiers(root, GetScannerModifiers());
    lv_label_set_text(v->layer, kb.online && kb.layer[0] ? kb.layer : "--");
    lv_label_set_text(v->layer_bold, kb.online && kb.layer[0] ? kb.layer : "--");
    lv_label_set_text(v->transport, kb.online ? (kb.usb ? "USB" : kb.ble ? "BLE" : "OFF") : "--");
    char text[12] = "--";
    if (kb.online)
        std::snprintf(text, sizeof(text), "%d", kb.profile);
    lv_label_set_text(v->profile, text);
    SetBattery(v->left, fight.online ? fight.left_battery : kb.online ? kb.left_battery : -1);
    SetBattery(v->right, fight.online ? fight.right_battery : kb.online ? kb.right_battery : -1);
    // Only the canonical advertisement supplies total WPM; independent hand
    // counters are not summed or substituted for the keyboard's total WPM.
    if (kb.online)
        std::snprintf(text, sizeof(text), "%d", kb.wpm);
    else
        std::snprintf(text, sizeof(text), "--");
    lv_label_set_text(v->wpm, text);
    lv_label_set_text(v->wpm_bold, text);
    const auto second = esp_timer_get_time() / 1000000;
    if (second != v->last_history) {
        v->last_history = second;
        std::move(v->history + 1, v->history + 9, v->history);
        v->history[8] = kb.online ? kb.wpm : 0;
        for (int i = 0; i < 9; ++i) {
            const int height = std::clamp(v->history[i] / 10, 1, 12);
            lv_obj_set_height(v->bars[i], height);
            lv_obj_set_y(v->bars[i], 232 - height);
        }
    }
}

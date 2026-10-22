/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define DASH_DATA_SIZE 18
#define DASH_EVENT "vldash"
#define DASH_PACKET_VERSION 2
#define DASH_PACKET_HEADER_SIZE 4
#define DASH_PACKET_SIZE (DASH_PACKET_HEADER_SIZE + DASH_DATA_SIZE)
#define DASH_OUTPUT_NONE 0
#define DASH_OUTPUT_USB 1
#define DASH_OUTPUT_BLE 2
#define DASH_BLE_PROFILES 5
#define DASH_CPU 1
#define DASH_RAM 2
#define DASH_TEMP 4
#define DASH_QUOTA5 8
#define DASH_QUOTA7 16
#define DASH_RESET 32
#define DASH_HOST_TTL_MS 90000u
/* v1, flags, sequence, cpu%, ram%, quota5 remaining%, quota7 remaining%, reserved,
 * signed temperature in 0.1 C (LE16), RAM used/total in 64 MiB (LE16 each),
 * seconds until five-hour-window reset (LE32). */
static inline uint16_t dash_u16(const uint8_t* p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
static inline uint32_t dash_u32(const uint8_t* p) {
    return (uint32_t)dash_u16(p) | (uint32_t)dash_u16(p + 2) << 16;
}
static inline void dash_put32(uint8_t* p, uint32_t v) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}
static inline bool dash_valid(const uint8_t* p, unsigned len) {
    if (!p || len != DASH_DATA_SIZE || p[0] != 1 || p[1] & ~63u || p[7])
        return false;
    if ((p[1] & DASH_CPU) && p[3] > 100)
        return false;
    if ((p[1] & DASH_RAM) &&
        (p[4] > 100 || !dash_u16(p + 12) || dash_u16(p + 10) > dash_u16(p + 12)))
        return false;
    if ((p[1] & DASH_QUOTA5) && p[5] > 100)
        return false;
    if ((p[1] & DASH_QUOTA7) && p[6] > 100)
        return false;
    int16_t t = (int16_t)dash_u16(p + 8);
    return !(p[1] & DASH_TEMP) || (t >= -500 && t <= 1500);
}
struct panel_status {
    bool connected, battery_valid, layer_valid;
    uint8_t battery, layer, output, profile;
    char side;
};
struct dashboard_status {
    struct panel_status keyboard;
    uint8_t data[DASH_DATA_SIZE];
    uint8_t history[12], count;
};

static inline void dash_age_data(uint8_t* out, const uint8_t* in, bool live, uint32_t age) {
    memcpy(out, in, DASH_DATA_SIZE);
    if (!live || age >= DASH_HOST_TTL_MS)
        out[1] = 0;
    else if (out[1] & DASH_RESET) {
        uint32_t sec = dash_u32(in + 14), elapsed = age / 1000;
        dash_put32(out + 14, sec > elapsed ? sec - elapsed : 0);
    }
}

/* Output describes the receiver's selected, connected HID endpoint, not the split link. */
static inline bool dash_output_valid(unsigned output, unsigned profile) {
    return output <= DASH_OUTPUT_BLE &&
           (output == DASH_OUTPUT_BLE ? (profile >= 1 && profile <= DASH_BLE_PROFILES)
                                      : profile == 0);
}
static inline bool dash_theme_packet_valid(const uint8_t* p, unsigned size) {
    return p && size == DASH_PACKET_SIZE && p[0] == DASH_PACKET_VERSION && p[1] <= 1 &&
           dash_output_valid(p[2], p[3]) && dash_valid(p + DASH_PACKET_HEADER_SIZE, DASH_DATA_SIZE);
}

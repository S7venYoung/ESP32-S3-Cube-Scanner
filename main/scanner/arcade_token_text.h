#pragma once
#include "arcade_symbols.h"

namespace ArcadeTokenText {
struct Crop {
    int x, y, w, h;
};
// Ink bounds of the main digit components in the existing number atlas.
// Never edit the generated atlas; its M/K/dot cells contain neighbouring ink.
constexpr Crop digits[] = {{5, 4, 27, 27}, {6, 4, 18, 29}, {0, 4, 28, 29}, {0, 4, 26, 28},
                           {0, 2, 30, 32}, {0, 4, 28, 29}, {0, 4, 25, 28}, {1, 4, 28, 29},
                           {0, 4, 26, 28}, {1, 4, 25, 29}};
inline bool Supported(const char* text) {
    if (!text || !*text || std::strlen(text) > 16)
        return false;
    bool number = false;
    for (const char* p = text; *p; ++p) {
        if (*p >= '0' && *p <= '9')
            number = true;
        else if (*p != '.' && *p != '-' && *p != 'M' && *p != 'K')
            return false;
    }
    return number || std::strcmp(text, "--") == 0;
}
inline int Advance(char c, int h) {
    if (c >= '0' && c <= '9')
        return std::max(3, digits[c - '0'].w * h / 30);
    if (c == '.')
        return std::max(3, h / 6);
    if (c == 'M' || c == 'K')
        return std::max(7, h * 3 / 4);
    return std::max(3, h / 2);
}
inline int Measure(const char* text, int h) {
    int width = 0;
    for (const char* p = text; *p; ++p)
        width += Advance(*p, h) + 2;
    return std::max(0, width - 2);
}
inline void Pixel(uint8_t* out, uint32_t color, unsigned alpha = 255) {
    out[0] = color;
    out[1] = color >> 8;
    out[2] = color >> 16;
    out[3] = alpha;
}
// One baseline for numbers, punctuation and a clear brush-outline unit.
inline void Paint(uint8_t* pixels, int width, int height, int stride, const char* text,
                  const uint8_t* atlas) {
    int h = std::max(4, height - 4);
    while (h > 4 && Measure(text, h) > width - 2)
        --h;
    const int baseline = height - 2, top = baseline - h;
    int origin = (width - Measure(text, h)) / 2;
    for (const char* p = text; *p; ++p) {
        const int w = Advance(*p, h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const int dx = origin + x, dy = top + y;
                if (dx < 0 || dx >= width || dy < 0 || dy >= height)
                    continue;
                auto* out = pixels + dy * stride + dx * 4;
                if (*p >= '0' && *p <= '9') {
                    const auto c = digits[*p - '0'];
                    unsigned alpha = 0, channels[3] = {};
                    for (int sy = 0; sy < 4; ++sy)
                        for (int sx = 0; sx < 4; ++sx) {
                            const int gx = c.x + std::min(c.w - 1, (x * 4 + sx) * c.w / (w * 4));
                            const int gy = c.y + std::min(c.h - 1, (y * 4 + sy) * c.h / (h * 4));
                            const auto* src = atlas + (((40 + *p - '0') * 36 + gy) * 32 + gx) * 4;
                            alpha += src[3];
                            for (int i = 0; i < 3; ++i)
                                channels[i] += src[i] * src[3];
                        }
                    out[3] = alpha / 16;
                    if (alpha)
                        for (int i = 0; i < 3; ++i)
                            out[i] = channels[i] / alpha;
                } else if (*p == 'M' || *p == 'K') {
                    const auto glyph = ArcadeSymbols::Letter(*p);
                    unsigned hits = 0, channels[3] = {};
                    for (int sy = 0; sy < 4; ++sy)
                        for (int sx = 0; sx < 4; ++sx) {
                            const float gy = (y + (sy + 0.5f) / 4) * 12.4f / h - 1.2f;
                            const float gx =
                                (x + (sx + 0.5f) / 4) * 12.4f / w - 1.7f - 0.1f * (10 - gy);
                            float edge = 100;
                            for (unsigned i = 0; i < glyph.count; ++i) {
                                const auto s = glyph.strokes[i];
                                const float dx = s.bx - s.ax, dy = s.by - s.ay;
                                const float t = std::clamp(((gx - s.ax) * dx + (gy - s.ay) * dy) /
                                                               std::max(0.01f, dx * dx + dy * dy),
                                                           0.0f, 1.0f);
                                const float ex = gx - s.ax - t * dx, ey = gy - s.ay - t * dy;
                                edge = std::min(edge, std::sqrt(ex * ex + ey * ey));
                            }
                            if (edge > 1.2f)
                                continue;
                            const uint32_t color =
                                edge <= 0.75f ? (gy < 4 ? 0xFF7448 : 0xFF442C) : 0xFFF3CD;
                            channels[0] += color & 255;
                            channels[1] += (color >> 8) & 255;
                            channels[2] += (color >> 16) & 255;
                            ++hits;
                        }
                    if (hits) {
                        for (int i = 0; i < 3; ++i)
                            out[i] = channels[i] / hits;
                        out[3] = hits * 255 / 16;
                    }
                } else if (*p == '.') {
                    const int size = std::max(2, h / 7);
                    if (y >= h - size && x < size)
                        Pixel(out, 0xFFF3CD);
                } else if (*p == '-' && y >= h / 2 - 1 && y <= h / 2 + 1)
                    Pixel(out, 0xFFF3CD);
            }
        origin += w + 2;
    }
}
}  // namespace ArcadeTokenText

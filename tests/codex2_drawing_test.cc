#include "../main/scanner/codex_draw_masks.h"
#include <cassert>
#include <iostream>
template<size_t N, size_t G> void Check(const uint8_t (&)[N], const MaskGlyph (&glyphs)[G]) {
    assert(G == 96);
    for (const auto& glyph : glyphs) {
        assert(glyph.offset + (glyph.width * glyph.height + 1) / 2 <= N);
        assert(glyph.advance < 2048);
    }
}
int main() {
    Check(mask_pixels_16, mask_glyphs_16);
    Check(mask_pixels_20, mask_glyphs_20);
    Check(mask_pixels_48, mask_glyphs_48);
    for (const auto* text : {"100%", "--%", "4295.0M", "OFFLINE", "WI-FI", "5 HOUR LEFT"}) {
        for (const auto* p = text; *p; ++p) assert(*p >= 32 && *p <= 127);
    }
    std::cout << "Three direct-drawing masks verified: 96 entries each, all coverage reads in bounds.\n";
}


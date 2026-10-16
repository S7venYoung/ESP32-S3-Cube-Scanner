#include <cassert>
#include <fstream>
#include <vector>
#include "../main/scanner/arcade_lettering.h"
int main(int argc, char** argv) {
    assert(ArcadeTokenText::Supported("1.6M"));
    assert(ArcadeTokenText::Supported("999.9K"));
    assert(!ArcadeTokenText::Supported("TODAY TOKENS"));
    constexpr int width = 104, height = 28, stride = width * 4 + 8;
    const char* samples[] = {"1.6M", "38.2M", "999.9K", "999", "--"};
    std::ofstream preview;
    if (argc > 1) {
        preview.open(argv[1], std::ios::binary);
        preview << "P6\n832 1120\n255\n";
    }
    for (const char* value : samples) {
        std::vector<uint8_t> pixels(stride * height + 16, 0xA5);
        ArcadeLettering::Paint(pixels.data(), width, height, stride, value, 0xFF442C);
        for (size_t i = stride * height; i < pixels.size(); ++i)
            assert(pixels[i] == 0xA5);
        int h = height - 4;
        while (h > 4 && ArcadeTokenText::Measure(value, h) > width - 2)
            --h;
        int x = (width - ArcadeTokenText::Measure(value, h)) / 2;
        for (const char* p = value; *p; ++p) {
            const int w = ArcadeTokenText::Advance(*p, h);
            int bottom = -1, count = 0;
            for (int y = 0; y < height; ++y)
                for (int i = std::max(0, x); i < std::min(width, x + w); ++i)
                    if (pixels[y * stride + i * 4 + 3] > 100) {
                        bottom = std::max(bottom, y);
                        ++count;
                    }
            assert(count > 0);
            if (*p != '-')
                assert(bottom >= height - 4 && bottom <= height - 3);
            if (*p == 'M' || *p == 'K')
                assert(count > 40);
            x += w + 2;
        }
        if (preview.is_open())
            for (int y = 0; y < height * 8; ++y)
                for (int x = 0; x < width * 8; ++x) {
                    const auto* p = pixels.data() + (y / 8) * stride + (x / 8) * 4;
                    const unsigned a = p[3];
                    const char rgb[] = {char((p[2] * a + 16 * (255 - a)) / 255),
                                        char((p[1] * a + 16 * (255 - a)) / 255),
                                        char((p[0] * a + 16 * (255 - a)) / 255)};
                    preview.write(rgb, 3);
                }
    }
}

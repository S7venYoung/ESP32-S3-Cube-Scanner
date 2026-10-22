#include "../main/scanner/arcade_controls.h"
#include <cassert>
#include <vector>

int main() {
    constexpr int width = 108, height = 25, stride = width * 4;
    for (int level : {-1, 0, 20, 50, 80, 100}) {
        std::vector<uint8_t> left(stride * height + 16, 0xA5);
        std::vector<uint8_t> right(stride * height + 16, 0xA5);
        ArcadeControls::Energy(left.data(), width, height, stride, level, false);
        ArcadeControls::Energy(right.data(), width, height, stride, level, true);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                for (int c = 0; c < 4; ++c)
                    assert(left[y * stride + x * 4 + c] ==
                           right[y * stride + (width - 1 - x) * 4 + c]);
        for (size_t i = stride * height; i < left.size(); ++i)
            assert(left[i] == 0xA5 && right[i] == 0xA5);
    }
    std::vector<uint8_t> pixels(stride * height);
    for (bool mirrored : {false, true}) {
        ArcadeControls::Energy(pixels.data(), width, height, stride, 50, mirrored);
        int outer = 0, center = 0;
        for (int y = 5; y < 20; ++y)
            for (int x = 5; x < width - 5; ++x) {
                const auto* p = pixels.data() + y * stride + x * 4;
                if (p[0] > 130 && p[1] > 100 && p[2] < 100) {
                    const bool outside = mirrored ? x >= width / 2 : x < width / 2;
                    (outside ? outer : center)++;
                }
            }
        assert(outer > center && outer > 0);
    }
}

#include "../main/scanner/arcade_controls.h"
#include <cassert>
#include <vector>
int main() {
    constexpr int w=108,h=25,stride=w*4;
    for(int percent:{-1,0,72,100}) {
        std::vector<uint8_t> p(stride*h+16,0xA5);
        ArcadeControls::Energy(p.data(),w,h,stride,percent);
        assert(p[3]==0); // Transparent outside the slanted outer frame.
        assert(p[(12*stride)+4*2+3]>0); // Left pointed tip.
        for(size_t i=stride*h;i<p.size();++i)assert(p[i]==0xA5);
    }
    std::vector<uint8_t> left(stride*h),right(stride*h);
    ArcadeControls::Energy(left.data(),w,h,stride,30,false);
    ArcadeControls::Energy(right.data(),w,h,stride,30,true);
    auto blue=[&](const std::vector<uint8_t>& p,int x){return p[17*stride+x*4]>100;};
    assert(blue(left,12) && !blue(left,85));
    assert(!blue(right,12) && blue(right,85));
    for(char c: {'^','~','@','#'}) {
        std::vector<uint8_t> p(28*20*4+16,0xA5);
        ArcadeControls::Modifier(p.data(),28,20,28*4,c,0xF3EEE5);
        unsigned visible=0;for(int i=3;i<28*20*4;i+=4)visible+=p[i]>0;
        assert(visible>20);
        for(size_t i=28*20*4;i<p.size();++i)assert(p[i]==0xA5);
    }
}

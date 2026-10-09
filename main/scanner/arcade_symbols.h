#pragma once
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <cstdint>
#include <cstring>

// Hand-authored brush centerlines, no font files or system-font fallback.
namespace ArcadeSymbols {
struct Stroke { float ax, ay, bx, by; };
struct Glyph {
    Stroke strokes[24]{};
    unsigned count = 0;
    Glyph(std::initializer_list<Stroke> lines) {
        for (auto line : lines) if (count < 24) strokes[count++] = line;
    }
};
inline Glyph Letter(unsigned char c) {
    if (c >= 'a' && c <= 'z') c -= 32;
    switch (c) {
        case 'A': return {{0,10,4,0},{4,0,8,10},{1,6,7,6}};
        case 'B': return {{0,0,0,10},{0,0,6,0},{6,0,8,2},{8,2,6,5},{0,5,6,5},{6,5,8,7},{8,7,7,9},{7,9,0,10}};
        case 'C': return {{8,1,6,0},{6,0,2,0},{2,0,0,3},{0,3,0,8},{0,8,2,10},{2,10,6,10},{6,10,8,9}};
        case 'D': return {{0,0,0,10},{0,0,5,0},{5,0,8,3},{8,3,8,7},{8,7,5,10},{5,10,0,10}};
        case 'E': return {{0,0,0,10},{0,0,8,0},{0,5,6,5},{0,10,8,10}};
        case 'F': return {{0,0,0,10},{0,0,8,0},{0,5,6,5}};
        case 'G': return {{8,1,6,0},{6,0,2,0},{2,0,0,3},{0,3,0,8},{0,8,2,10},{2,10,7,10},{7,10,8,6},{8,6,4,6}};
        case 'H': return {{0,0,0,10},{8,0,8,10},{0,5,8,5}};
        case 'I': return {{1,0,7,0},{4,0,4,10},{1,10,7,10}};
        case 'J': return {{1,0,8,0},{7,0,7,8},{7,8,5,10},{5,10,2,10},{2,10,0,8}};
        case 'K': return {{0,0,0,10},{8,0,0,6},{2,5,8,10}};
        case 'L': return {{0,0,0,10},{0,10,8,10}};
        case 'M': return {{0,10,0,0},{0,0,4,5},{4,5,8,0},{8,0,8,10}};
        case 'N': return {{0,10,0,0},{0,0,8,10},{8,10,8,0}};
        case 'O': case '0': return {{2,0,6,0},{6,0,8,2},{8,2,8,8},{8,8,6,10},{6,10,2,10},{2,10,0,8},{0,8,0,2},{0,2,2,0}};
        case 'P': return {{0,10,0,0},{0,0,6,0},{6,0,8,2},{8,2,7,5},{7,5,0,5}};
        case 'Q': return {{2,0,6,0},{6,0,8,2},{8,2,8,8},{8,8,6,10},{6,10,2,10},{2,10,0,8},{0,8,0,2},{0,2,2,0},{5,7,9,11}};
        case 'R': return {{0,10,0,0},{0,0,6,0},{6,0,8,2},{8,2,7,5},{7,5,0,5},{4,5,8,10}};
        case 'S': return {{8,0,2,0},{2,0,0,2},{0,2,1,4},{1,4,7,6},{7,6,8,8},{8,8,6,10},{6,10,0,10}};
        case '5': return {{8,0,0,0},{0,0,0,5},{0,5,6,5},{6,5,8,7},{8,7,8,8},{8,8,6,10},{6,10,0,10}};
        case 'T': return {{0,0,8,0},{4,0,4,10}};
        case 'U': return {{0,0,0,8},{0,8,2,10},{2,10,6,10},{6,10,8,8},{8,8,8,0}};
        case 'V': return {{0,0,4,10},{4,10,8,0}};
        case 'W': return {{0,0,1,10},{1,10,4,6},{4,6,7,10},{7,10,8,0}};
        case 'X': return {{0,0,8,10},{8,0,0,10}};
        case 'Y': return {{0,0,4,5},{8,0,4,5},{4,5,4,10}};
        case 'Z': return {{0,0,8,0},{8,0,0,10},{0,10,8,10}};
        case '1': return {{1,2,4,0},{4,0,4,10},{1,10,7,10}};
        case '2': return {{0,2,2,0},{2,0,6,0},{6,0,8,2},{8,2,7,4},{7,4,0,10},{0,10,8,10}};
        case '3': return {{0,0,6,0},{6,0,8,2},{8,2,6,5},{6,5,2,5},{6,5,8,7},{8,7,7,9},{7,9,0,10}};
        case '4': return {{6,0,0,6},{0,6,8,6},{6,0,6,10}};
        case '6': return {{7,0,3,0},{3,0,0,4},{0,4,0,8},{0,8,2,10},{2,10,6,10},{6,10,8,8},{8,8,8,6},{8,6,6,5},{6,5,0,5}};
        case '7': return {{0,0,8,0},{8,0,3,10}};
        case '8': return {{2,0,6,0},{6,0,8,2},{8,2,6,5},{6,5,2,5},{2,5,0,2},{0,2,2,0},{2,5,0,8},{0,8,2,10},{2,10,6,10},{6,10,8,8},{8,8,6,5}};
        case '9': return {{8,5,2,5},{2,5,0,3},{0,3,0,2},{0,2,2,0},{2,0,6,0},{6,0,8,2},{8,2,8,8},{8,8,6,10},{6,10,1,10}};
        case '%': return {{0,10,8,0},{1,1,2,1},{2,1,2,3},{2,3,0,3},{0,3,0,1},{0,1,1,1},{6,7,8,7},{8,7,8,9},{8,9,6,9},{6,9,6,7}};
        case '-': return {{1,5,7,5}};
        case '+': return {{1,5,7,5},{4,2,4,8}};
        case '.': return {{3,10,4,10}};
        case ':': return {{3,3,4,3},{3,8,4,8}};
        case '/': return {{0,10,8,0}};
        case '^': return {{0,6,4,2},{4,2,8,6}};
        case '~': return {{0,2,3,2},{3,2,6,8},{6,8,9,8},{6,2,9,2}};
        case '#': return {{0,5,4,1},{4,1,8,5},{8,5,6,5},{6,5,6,10},{6,10,2,10},{2,10,2,5},{2,5,0,5}};
        case '@': return {{2,2,2,8},{2,8,6,8},{6,8,6,2},{6,2,2,2},{2,2,2,0},{2,0,0,0},{0,0,0,2},{0,2,8,2},{8,2,8,0},{8,0,6,0},{6,0,6,10},{6,10,8,10},{8,10,8,8},{8,8,0,8},{0,8,0,10},{0,10,2,10}};
        default: return {};
    }
}
inline float Edge(const Glyph& glyph,float x,float y) {
    float nearest=100;
    for(unsigned i=0;i<glyph.count;++i){
        const auto& s=glyph.strokes[i];const float dx=s.bx-s.ax,dy=s.by-s.ay;
        const float t=std::clamp(((x-s.ax)*dx+(y-s.ay)*dy)/std::max(0.01f,dx*dx+dy*dy),0.0f,1.0f);
        const float ex=x-s.ax-t*dx,ey=y-s.ay-t*dy;
        // Tapered endpoints and slight asymmetric brush pressure, not grid pixels.
        const float radius=1.22f+0.28f*std::sin(t*3.14159265f)+(i%3)*0.045f;
        nearest=std::min(nearest,std::sqrt(ex*ex+ey*ey)-radius);
    }
    return nearest;
}
inline void Paint(uint8_t* pixels,int width,int height,int stride,const char* text,uint32_t face) {
    std::memset(pixels,0,stride*height);
    const size_t length=std::strlen(text);
    const float sy=(height-5)/12.0f;
    const float sx=std::min(sy*0.82f,(width-4)/std::max(1.0f,static_cast<float>(length*11+3)));
    float origin=std::max(2.0f,(width-(length*11+2)*sx)/2);
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p){
        const auto glyph=Letter(*p);
        if(glyph.count){
            const int begin=std::max(0,static_cast<int>(origin-2*sx));
            const int end=std::min(width,static_cast<int>(origin+14*sx+2));
            for(int y=0;y<height;++y)for(int x=begin;x<end;++x){
                unsigned hits=0,rr=0,gg=0,bb=0;
                for(int sub=0;sub<4;++sub){
                    const float gy=(y+(sub/2)*0.5f+0.25f-2)/sy;
                    const float gx=(x+(sub%2)*0.5f+0.25f-origin)/sx-0.26f*(10-gy);
                    const float edge=Edge(glyph,gx,gy);
                    if(edge>0.78f)continue;
                    uint32_t color=edge<=0?face:edge<=0.26f?0xFFF3CD:0x18130D;
                    if(edge<=0 && face==0xFF442C && gy<4)color=0xFF7448;
                    rr+=(color>>16)&255;gg+=(color>>8)&255;bb+=color&255;++hits;
                }
                if(hits){auto* q=pixels+y*stride+x*4;q[0]=bb/hits;q[1]=gg/hits;q[2]=rr/hits;q[3]=hits*255/4;}
            }
        }
        origin+=11*sx;
    }
}
} // namespace ArcadeSymbols

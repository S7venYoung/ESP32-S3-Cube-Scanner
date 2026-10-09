#pragma once
#include "arcade_symbols.h"
#include "arcade_controls.h"
#include "arcade_assets/arcade_glyphs.inc"
namespace ArcadeLettering {
inline int Index(unsigned char c) {
    if(c>='a' && c<='z') c-=32;
    const char* alphabet="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ%.-+";
    const char* found=std::strchr(alphabet,c);
    return c && found ? int(found-alphabet) : -1;
}
// Reference-derived artwork rendered directly into an alpha canvas, no fonts.
inline void Paint(uint8_t* pixels,int width,int height,int stride,const char* text,uint32_t color) {
    if(!pixels || width<=0 || height<=0 || stride<width*4)return;
    std::memset(pixels,0,size_t(stride)*height);
    if(!text || !*text)return;
    if(std::strpbrk(text,"^~@#")) {
        ArcadeControls::Modifier(pixels,width,height,stride,text[0],color);return;
    }
    const int count=std::strlen(text);
    const int advance=std::max(1,std::min(width/count,height*8/10));
    const int start=(width-advance*count)/2;
    for(int i=0;i<count;++i) {
        int index=Index(text[i]);if(index<0)continue;
        const bool red=color==0xFF442C;
        if(red && index<10)index+=40;
        if(red && text[i]=='M')index=50;
        if(red && text[i]=='K')index=51;
        if(red && text[i]=='.')index=52;
        for(int y=0;y<height;++y)for(int x=0;x<advance;++x) {
            const int dx=start+i*advance+x;if(dx<0 || dx>=width)continue;
            auto* out=pixels+y*stride+dx*4;
            unsigned alpha=0,channels[3]={};
            // Area-sample the artwork instead of dropping narrow strokes.
            for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx) {
                const int gx=std::min(31,((x*4+sx)*32)/(advance*4));
                const int gy=std::min(35,((y*4+sy)*36)/(height*4));
                const auto* source=arcade_glyph_pixels+((index*36+gy)*32+gx)*4;
                alpha+=source[3];
                for(int c=0;c<3;++c)channels[c]+=source[c]*source[3];
            }
            out[3]=alpha/16;
            if(alpha)for(int c=0;c<3;++c)out[c]=channels[c]/alpha;
            if(color==0x21CEEE || color==0xFFBF18 || (red && (index<40 || index>=50))) {
                out[0]=color;out[1]=color>>8;out[2]=color>>16;
            }
        }
    }
}
}

#include "../main/scanner/arcade_lettering.h"
#include <cassert>
#include <vector>
int main(){
    for(unsigned char c: "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789%+-.:/^~@#"){
        if(!c)continue;auto glyph=ArcadeSymbols::Letter(c);assert(glyph.count>0 && glyph.count<=24);
        assert(ArcadeSymbols::Edge(glyph,-100,-100)>0);
        const auto s=glyph.strokes[0];assert(ArcadeSymbols::Edge(glyph,s.ax,s.ay)<0);
    }
    assert(ArcadeLettering::Index(' ')==-1);
    for(const char* text:{"TODAY TOKENS","38.2M","100%","--","CODEX FIGHT"}){
        constexpr int width=104,height=28,stride=width*4+8;
        std::vector<uint8_t> pixels(stride*height+16,0xA5);
        ArcadeLettering::Paint(pixels.data(),width,height,stride,text,0xFF442C);
        unsigned transparent=0,opaque=0;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){
            transparent+=pixels[y*stride+x*4+3]==0;opaque+=pixels[y*stride+x*4+3]>200;
        }
        assert(transparent>width*height/3 && opaque>0);
        for(size_t i=stride*height;i<pixels.size();++i)assert(pixels[i]==0xA5);
        ArcadeLettering::Paint(pixels.data(),width,height,stride,"",0xFFFFFF);
        for(size_t i=0;i<stride*height;++i)assert(pixels[i]==0);
    }
}

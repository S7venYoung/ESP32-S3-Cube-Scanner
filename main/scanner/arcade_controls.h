#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace ArcadeControls {
struct Point {float x,y;};
inline float Segment(float x,float y,Point a,Point b) {
    const float dx=b.x-a.x,dy=b.y-a.y;
    const float t=std::clamp(((x-a.x)*dx+(y-a.y)*dy)/std::max(.001f,dx*dx+dy*dy),0.f,1.f);
    return std::hypot(x-a.x-t*dx,y-a.y-t*dy);
}
template<size_t N> inline float Outline(float x,float y,const Point (&p)[N]) {
    float d=1000;for(size_t i=0;i<N;++i)d=std::min(d,Segment(x,y,p[i],p[(i+1)%N]));return d;
}
template<size_t N> inline bool Inside(float x,float y,const Point (&p)[N]) {
    bool in=false;
    for(size_t i=0,j=N-1;i<N;j=i++)if((p[i].y>y)!=(p[j].y>y) &&
        x<(p[j].x-p[i].x)*(y-p[i].y)/(p[j].y-p[i].y)+p[i].x)in=!in;
    return in;
}
inline void Pixel(uint8_t* p,int stride,int x,int y,uint32_t rgb,unsigned alpha=255) {
    auto* q=p+y*stride+x*4;q[0]=rgb;q[1]=rgb>>8;q[2]=rgb>>16;q[3]=alpha;
}
// Smooth stroked Control, Option, Command and Shift, matching the reference.
inline bool Symbol(float x,float y,char symbol) {
    float d=100;
    if(symbol=='^') {
        d=std::min(Segment(x,y,{2,14},{10,6}),Segment(x,y,{10,6},{18,14}));
    } else if(symbol=='~') {
        const Point paths[][2]={{{2,5},{7,5}},{{7,5},{12,15}},{{12,15},{18,15}},{{13,5},{18,5}}};
        for(auto& path:paths)d=std::min(d,Segment(x,y,path[0],path[1]));
    } else if(symbol=='#') {
        const Point p[]={{10,3},{18,11},{13,11},{13,18},{7,18},{7,11},{2,11}};
        d=Outline(x,y,p);
    } else if(symbol=='@') {
        // Four round loops joined by the central crossing rectangle.
        for(int i=0;i<4;++i) {
            const float cx=(i&1)?15:5,cy=(i&2)?15:5;
            d=std::min(d,std::abs(std::hypot(x-cx,y-cy)-3));
        }
        const Point links[][2]={{{5,8},{15,8}},{{5,12},{15,12}},{{8,5},{8,15}},{{12,5},{12,15}}};
        for(auto& path:links)d=std::min(d,Segment(x,y,path[0],path[1]));
    }
    return d<1.1f;
}
inline void Modifier(uint8_t* p,int w,int h,int stride,char symbol,uint32_t rgb) {
    std::memset(p,0,size_t(stride)*h);
    const float scale=std::min(w,h)/22.f;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        unsigned coverage=0;
        for(float sy:{.25f,.75f})for(float sx:{.25f,.75f})
            coverage+=Symbol((x+sx-w/2.f)/scale+10,(y+sy-h/2.f)/scale+10,symbol);
        if(coverage)Pixel(p,stride,x,y,rgb,coverage*255/4);
    }
}
// Reference layout: outlined upper label panel and inset slanted blue cells.
inline void Energy(uint8_t* p,int w,int h,int stride,int percent) {
    std::memset(p,0,size_t(stride)*h);percent=std::clamp(percent,-1,100);
    const Point outer[]={{7,1},{100,1},{107,10},{103,17},{98,24},{7,24},{1,12}};
    const Point track[]={{7,12},{100,12},{104,17},{99,23},{7,23},{3,17}};
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        unsigned count=0,rr=0,gg=0,bb=0;
        for(float sy:{.25f,.75f})for(float sx:{.25f,.75f}) {
            const float px=(x+sx)*108/w,py=(y+sy)*25/h;
            if(!Inside(px,py,outer) && Outline(px,py,outer)>.35f)continue;
            uint32_t color=0x080D12;
            if(Outline(px,py,outer)<.45f)color=py<12?0xE4C591:0xBF8F49;
            if(Inside(px,py,track)) {
                color=0x16212B;
                const float u=px-7+(py-13)*.38f;
                const int cell=int(u/9.25f);
                if(py>14 && py<21 && cell>=0 && cell<10 && std::fmod(u,9.25f)>1.1f) {
                    color=percent>=0 && cell*10<percent ? (py<17?0x31DDF8:0x009FE0):0x29343E;
                }
            }
            if(Outline(px,py,track)<.45f)color=0xECC56F;
            ++count;rr+=(color>>16)&255;gg+=(color>>8)&255;bb+=color&255;
        }
        if(count)Pixel(p,stride,x,y,(rr/count)<<16|(gg/count)<<8|bb/count,count*255/4);
    }
}
}

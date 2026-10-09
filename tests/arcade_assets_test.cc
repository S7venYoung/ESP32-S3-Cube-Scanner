#include <cassert>
#include <cstdint>
#include <cstddef>
#include "../main/scanner/arcade_assets/arcade_dojo.inc"
#include "../main/scanner/arcade_assets/arcade_frame_0_0.inc"
#include "../main/scanner/arcade_assets/arcade_frame_0_1.inc"
#include "../main/scanner/arcade_assets/arcade_frame_0_2.inc"
#include "../main/scanner/arcade_assets/arcade_frame_0_3.inc"
#include "../main/scanner/arcade_assets/arcade_frame_0_4.inc"
#include "../main/scanner/arcade_assets/arcade_frame_0_5.inc"
#include "../main/scanner/arcade_assets/arcade_frame_0_6.inc"
#include "../main/scanner/arcade_assets/arcade_frame_0_7.inc"
#include "../main/scanner/arcade_assets/arcade_frame_0_8.inc"
#include "../main/scanner/arcade_assets/arcade_frame_1_0.inc"
#include "../main/scanner/arcade_assets/arcade_frame_1_1.inc"
#include "../main/scanner/arcade_assets/arcade_frame_1_2.inc"
#include "../main/scanner/arcade_assets/arcade_frame_1_3.inc"
#include "../main/scanner/arcade_assets/arcade_frame_1_4.inc"
#include "../main/scanner/arcade_assets/arcade_frame_1_5.inc"
#include "../main/scanner/arcade_assets/arcade_frame_1_6.inc"
#include "../main/scanner/arcade_assets/arcade_frame_1_7.inc"
#include "../main/scanner/arcade_assets/arcade_frame_1_8.inc"
template<size_t N> void Check(const uint8_t (&runs)[N],const uint32_t* palette) {
    assert(N%2==0);size_t pixels=0;bool transparent=false,visible=false;
    for(size_t i=0;i<N;i+=2){assert(runs[i]>0);assert(runs[i+1]<128);pixels+=runs[i];
        transparent|=(palette[runs[i+1]]>>24)==0;visible|=(palette[runs[i+1]]>>24)>200;}
    assert(pixels==96*104 && transparent && visible);
}
#define CHECK(s,p) Check(arcade_frame_##s##_##p##_runs,arcade_frame_##s##_##p##_palette)
int main(){CHECK(0,0);CHECK(0,1);CHECK(0,2);CHECK(0,3);CHECK(0,4);CHECK(0,5);
           CHECK(1,0);CHECK(1,1);CHECK(1,2);CHECK(1,3);CHECK(1,4);CHECK(1,5);
           CHECK(0,6);CHECK(0,7);CHECK(0,8);CHECK(1,6);CHECK(1,7);CHECK(1,8);}

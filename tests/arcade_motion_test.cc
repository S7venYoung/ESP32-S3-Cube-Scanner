#include "../main/scanner/arcade_motion.h"
#include <cassert>
int main() {
    ArcadeMotion left, right;
    assert(ArcadeIdleFrame(0,0)==0);
    assert(ArcadeIdleFrame(260,0)==1);
    assert(ArcadeIdleFrame(520,0)==0);
    assert(ArcadeIdleFrame(780,0)==2);
    assert(ArcadeIdleFrame(1040,0)==0);
    assert(ArcadeIdleFrame(100,0)!=ArcadeIdleFrame(100,1));
    left.Reset(0); right.Reset(0);
    bool super=false, punch=false, kick=false, fireball=false, uppercut=false;
    for(uint32_t now=80;now<=16000;now+=80){
        left.Step(now,100,true);right.Step(now,0,true);
        assert(right.pose==0);assert(left.pose>=0 && left.pose<=5);
        super|=left.pose==5;punch|=left.pose==1;kick|=left.pose==2;fireball|=left.pose==3;uppercut|=left.pose==4;
    }
    assert(super && punch && kick && fireball && uppercut);
    left.Step(16080,100,false);assert(left.pose==0 && left.charge==0);
    left.Reset(UINT32_MAX-20);left.Step(50,70,true);assert(left.elapsed==71);
}

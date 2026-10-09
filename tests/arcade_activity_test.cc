#include "../main/scanner/arcade_activity.h"
#include <cassert>
int main(){
    ArcadeActivity a;
    assert(!a.Step(0,true,0,0));
    assert(a.Step(100,true,1,0));
    assert(a.Step(200,true,0,10));
    assert(a.Step(1699,true,0,0));
    assert(!a.Step(1700,true,0,0));
    assert(a.Step(2000,true,5,0));
    assert(!a.Step(2001,false,5,0));
    assert(a.Step(0xFFFFFF00,true,5,0));
    assert(a.Step(0x100,true,0,0));
    assert(!a.Step(0x600,true,0,0));
}

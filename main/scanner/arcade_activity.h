#pragma once
#include <cstdint>
struct ArcadeActivity {
    bool hidden=false;
    uint32_t last_active=0;
    bool Step(uint32_t now,bool online,int left,int right) {
        if(!online){hidden=false;return false;}
        if(left>0 || right>0){hidden=true;last_active=now;}
        else if(hidden && uint32_t(now-last_active)>=1500)hidden=false;
        return hidden;
    }
};

#include "../main/scanner/fight_telemetry.h"
#include <cassert>
int main() {
    uint8_t packet[17]={255,255,0xab,0xce,1,7,1,2,3,4,30,70,72,100,0x88,1,0};
    FightTelemetry value;
    assert(ParseFightTelemetry(packet,17,value));
    assert(value.left_wpm==30 && value.right_wpm==70 && value.left_battery==72 && value.right_battery==100);
    assert(value.modifiers==4 && value.keyboard_id==0x04030201);
    packet[5]=1;
    assert(ParseFightTelemetry(packet,17,value) && value.left_battery==-1);
    packet[10]=0; packet[11]=255;
    assert(ParseFightTelemetry(packet,17,value) && value.left_wpm==0 && value.right_wpm==255);
    assert(!ParseFightTelemetry(packet,16,value));
    packet[4]=2; assert(!ParseFightTelemetry(packet,17,value));
    packet[4]=1; packet[5]=7; packet[12]=101;
    assert(!ParseFightTelemetry(packet,17,value));
    assert(FightActionForWpm(-1)==0 && FightActionForWpm(4)==0 && FightActionForWpm(5)==1);
    assert(FightActionForWpm(29)==1 && FightActionForWpm(30)==2 && FightActionForWpm(70)==3);
}

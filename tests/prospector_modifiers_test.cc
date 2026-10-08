#include "../main/scanner/prospector_modifiers.h"
#include <cassert>

int main() {
    uint8_t packet[26] = {0xff,0xff,0xab,0xcd,0x22};
    uint8_t mac = 99;
    assert(ParseProspectorModifiers(packet,26,mac) && mac==0);
    const uint8_t hid[] = {1,4,8,2,16,64,128,32};
    for (int i=0;i<8;++i) {
        packet[23]=hid[i];
        assert(ParseProspectorModifiers(packet,26,mac) && mac==(1u<<(i%4)));
    }
    packet[23]=255;
    assert(ParseProspectorModifiers(packet,26,mac) && mac==15);
    packet[10]=2;
    assert(!ParseProspectorModifiers(packet,26,mac));
    packet[10]=1;
    assert(!ParseProspectorModifiers(packet,25,mac));
    packet[4]=0x30;
    assert(!ParseProspectorModifiers(packet,26,mac));
    assert(!ParseProspectorModifiers(nullptr,26,mac));
}

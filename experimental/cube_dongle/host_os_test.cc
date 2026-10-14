#include "host_os.h"
#include <cassert>
int main() {
    using namespace cube_dongle;
    assert(ClassifyUsbOs(0) == 0);
    assert(ClassifyUsbOs(UsbDescriptorObservation(3, 2) | UsbDescriptorObservation(3, 24)) == 2);
    assert(ClassifyUsbOs(UsbDescriptorObservation(15, 255) | UsbDescriptorObservation(3, 255)) ==
           1);
    assert(ClassifyUsbOs(UsbDescriptorObservation(15, 5) | UsbDescriptorObservation(3, 255)) == 3);
    assert(UsbDescriptorObservation(1, 18) == 0);
}

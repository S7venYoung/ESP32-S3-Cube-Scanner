#include <iostream>
#include "custom_settings.h"
#include "default_layers.h"
#include "fast_keymap.h"
#include "runtime_pointer.h"
#include "runtime_sensors.h"
using namespace cube_dongle::studio;
int main() {
    Rpc rpc;
    DefaultLayers defaults;
    CustomSettings settings;
    PointerSettings pointers;
    SensorSettings sensors;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.size() < 2 || line.size() % 2)
            return 1;
        Bytes bytes;
        for (size_t i = 2; i < line.size(); i += 2)
            bytes.push_back(std::stoul(line.substr(i, 2), nullptr, 16));
        const auto save = [](const Bytes&) { return true; };
        Bytes out;
        switch (std::stoul(line.substr(0, 2), nullptr, 16)) {
            case 0:
                out = FastKeymap(bytes, rpc);
                break;
            case 1:
                out = defaults.Handle(bytes, true, rpc.model, 0, save);
                break;
            case 2:
                out = defaults.Os(bytes, true, 0, 3, true, false, save);
                break;
            case 3:
                out = settings.Handle(bytes, true, 3, save, [](const Bytes&) {});
                break;
            case 4:
                out = pointers.Handle(bytes, true, 1, rpc.model, save, [](const Bytes&) {});
                break;
            case 5:
                out = sensors.Handle(bytes, true, 1, rpc.model, save);
                break;
            default:
                return 1;
        }
        static const char hex[] = "0123456789abcdef";
        for (auto byte : out)
            std::cout << hex[byte >> 4] << hex[byte & 15];
        std::cout << std::endl;
    }
}

#pragma once

#include <cstdint>
#include <string>

struct ScannerDevice {
    std::string name;
    std::string address;
    int rssi = 0;
    int64_t last_seen_us = 0;
};

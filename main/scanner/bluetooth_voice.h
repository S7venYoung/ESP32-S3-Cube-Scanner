#pragma once
#include <string>
// Exact local STT commands only; no voice bond deletion or loose substring match.
inline std::string CubeBluetoothVoiceCommand(std::string text) {
    for (const auto* token : {" ", "\t", "\n", ",", ".", "!", "?", "，", "。", "！", "？"}) {
        size_t at;
        while ((at = text.find(token)) != std::string::npos)
            text.erase(at, std::string(token).size());
    }
    for (const auto* prefix : {"你好小智", "小智", "请", "帮我"})
        if (text.rfind(prefix, 0) == 0)
            text.erase(0, std::string(prefix).size());
    if (text == "切换下一个蓝牙" || text == "切换到下一个蓝牙" || text == "下一个蓝牙" ||
        text == "切换下一个蓝牙连接" || text == "切换到下一个蓝牙连接")
        return "DONGLE BT NEXT";
    for (const auto* prefix :
         {"切换到蓝牙配置", "切换蓝牙配置", "切换到蓝牙", "切换蓝牙", "连接蓝牙"}) {
        if (text.rfind(prefix, 0) != 0)
            continue;
        auto number = text.substr(std::string(prefix).size());
        if (number.size() >= 3 && number.substr(number.size() - 3) == "号")
            number.resize(number.size() - 3);
        const char* chinese[] = {"一", "二", "三", "四", "五"};
        for (int slot = 0; slot < 5; ++slot)
            if (number == chinese[slot] || number == std::to_string(slot + 1))
                return "DONGLE BT " + std::to_string(slot + 1);
    }
    return {};
}

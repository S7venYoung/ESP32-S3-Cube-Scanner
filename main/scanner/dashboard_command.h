#pragma once
#include <algorithm>
#include <string>

enum class DashboardVoiceCommand { None, Codex, Macintosh, Arcade };
inline DashboardVoiceCommand DashboardVoiceIntent(std::string text) {
    for (const auto* token : {" ", "\t", "\n", ",", ".", "!", "?", "，", "。", "！", "？"}) {
        size_t at;
        while ((at = text.find(token)) != std::string::npos) text.erase(at, std::string(token).size());
    }
    for (auto& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    for (const auto* prefix : {"你好小智", "小智", "请", "帮我"}) {
        if (text.rfind(prefix, 0) == 0) text.erase(0, std::string(prefix).size());
    }
    for (const auto* prefix : {"切换到", "切换为", "切换", "换成"}) {
        if (text.rfind(prefix, 0) != 0) continue;
        const auto name = text.substr(std::string(prefix).size());
        if (name == "迈克主题" || name == "麦金塔主题" || name == "麦金塔" ||
            name == "mac主题" || name == "macintosh主题") return DashboardVoiceCommand::Macintosh;
        if (name == "codex主题" || name == "codex") return DashboardVoiceCommand::Codex;
        if (name == "街机主题" || name == "街机" || name == "格斗主题" || name == "街霸主题" || name == "arcade主题") return DashboardVoiceCommand::Arcade;
    }
    return DashboardVoiceCommand::None;
}

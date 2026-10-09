#include "../main/scanner/dashboard_command.h"
#include <cassert>

int main() {
    using Command = DashboardVoiceCommand;
    for (const auto* text : {"切换迈克主题", "你好小智，切换麦金塔主题。", "请切换到 Mac 主题", "换成macintosh主题"})
        assert(DashboardVoiceIntent(text) == Command::Macintosh);
    assert(DashboardVoiceIntent("切换Codex主题") == Command::Codex);
    assert(DashboardVoiceIntent("你好小智，切换街机主题。") == Command::Arcade);
    assert(DashboardVoiceIntent("切换到格斗主题") == Command::Arcade);
    for (const auto* text : {"不要切换迈克主题", "怎么切换迈克主题？", "麦金塔主题", "切换迈克主题是什么", ""})
        assert(DashboardVoiceIntent(text) == Command::None);
}

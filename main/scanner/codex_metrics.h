#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

struct CodexMetrics {
    int left = -1;
    int week_left = -1;
    int64_t tokens = -1;
    unsigned age_seconds = 0;
    unsigned ttl_seconds = 120;
    int64_t reset_at = -1;  // Official primary window reset epoch, never inferred.
    int utc_offset_seconds = 0;  // Mac timezone offset at that reset instant.
};

// Shared by both transports; reject the entire frame rather than clamp bad data.
inline bool ParseCodexFrame(const char* line, CodexMetrics& result) {
    CodexMetrics value;
    if (line == nullptr) return false;
    std::istringstream input(line);
    std::string command, extra;
    int64_t age = 0, ttl = 120;
    if (!(input >> command)) return false;
    if (command == "CODEX2" || command == "CODEX3") {
        if (!(input >> value.left >> value.week_left >> value.tokens >> age >> ttl)) return false;
        if (command == "CODEX3" && !(input >> value.reset_at >> value.utc_offset_seconds)) return false;
    } else if (command == "CODEX") {
        if (!(input >> value.left >> value.tokens)) return false;
        input >> std::ws;
        if (!input.eof() && !(input >> value.week_left)) return false;
    } else return false;
    if (input >> extra) return false;
    if (value.left < -1 || value.left > 100 || value.week_left < -1 || value.week_left > 100 ||
        value.tokens < -1 || value.tokens > UINT32_MAX || age < 0 || age > 86400 || ttl < 30 || ttl > 3600) return false;
    if ((value.reset_at != -1 && (value.reset_at < 1577836800 || value.reset_at > UINT32_MAX)) ||
        value.utc_offset_seconds < -50400 || value.utc_offset_seconds > 50400) return false;
    value.age_seconds = static_cast<unsigned>(age);
    value.ttl_seconds = static_cast<unsigned>(ttl);
    result = value;
    return true;
}

inline void FormatCodexReset(const CodexMetrics& metrics, bool online, char* text, size_t size) {
    if (!online || metrics.left < 0 || metrics.reset_at < 0) {
        std::snprintf(text, size, "--:--");
        return;
    }
    const int64_t local = metrics.reset_at + metrics.utc_offset_seconds;
    const unsigned minutes = static_cast<unsigned>((local % 86400) / 60);
    std::snprintf(text, size, "%02u:%02u", minutes / 60, minutes % 60);
}

struct CodexSnapshot {
    CodexMetrics metrics;
    const char* transport = "OFFLINE";
    bool online = false;
};

void StartCodexSync();
CodexSnapshot GetCodexSnapshot();

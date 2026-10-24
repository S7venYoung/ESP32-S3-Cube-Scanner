#pragma once
#include <string>
// RTC breadcrumbs only: never erase NVS or replace a panic backtrace.
void CubeBootTraceStart(bool retain);
void CubeBootTraceStage(unsigned stage);
std::string CubeBootTraceStatus();

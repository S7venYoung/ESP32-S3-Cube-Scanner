#pragma once
#include <cstdint>
#include <string>
#include "fight_telemetry.h"
#include "keyboard_status.h"

// Owns NimBLE only in opt-in split-client builds. USB output is separately gated.
void StartCubeSplitBench();
// Atomic diagnostic: bit0/1 configured L/R, bit2/3 ready L/R, bits8..15 pressed.
uint32_t CubeSplitBenchStatus();
std::string CubeSplitBenchBind(const std::string& command);
FightTelemetry CubeSplitFightTelemetry();
KeyboardStatus CubeSplitKeyboardStatus();

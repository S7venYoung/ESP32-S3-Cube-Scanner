#pragma once
#include <cstdint>
#include <string>

// Owns the NimBLE host only in the opt-in bench build. Never emits HID input.
void StartCubeSplitBench();
// Atomic diagnostic: bit0/1 configured L/R, bit2/3 ready L/R, bits8..15 pressed.
uint32_t CubeSplitBenchStatus();
std::string CubeSplitBenchBind(const std::string& command);

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "fight_telemetry.h"
#include "keyboard_status.h"

// Owns NimBLE only in opt-in split-client builds. USB output is separately gated.
void StartCubeSplitBench();
void CubeSplitInputTick();       // NimBLE host thread only; flush delayed combo candidates.
void CubeSplitCancelHoldTaps();  // NimBLE host thread only; no replay after disconnect.
// Atomic diagnostic: bit0/1 configured L/R, bit2/3 ready L/R, bits8..15 pressed.
uint32_t CubeSplitBenchStatus();
std::string CubeSplitBenchBind(const std::string& command);
FightTelemetry CubeSplitFightTelemetry();
KeyboardStatus CubeSplitKeyboardStatus();
bool CubeSplitIsKeyboard(const uint8_t* address, uint8_t type);
bool CubeSplitForgetBonds();  // Secured DYA action; removes only two split peers, not computer
                              // profiles.
bool CubeSplitQueueRelay(unsigned source, const std::string& name,
                         const std::vector<uint8_t>& data);

// Validated, volatile computer metrics from USB or authenticated Wi-Fi.
bool CubeSplitUpdateHostMetrics(const uint8_t* data, size_t size);

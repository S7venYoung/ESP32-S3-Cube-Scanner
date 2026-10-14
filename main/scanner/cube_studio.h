#pragma once
#include <functional>
#include "../../experimental/cube_dongle/combo_engine.h"
#include "../../experimental/cube_dongle/report_queue.h"
#include "../../experimental/cube_dongle/split_state.h"
void CubeStudioInitialize();
void CubeStudioPointerHardware(unsigned index, bool present);
void CubeStudioPointerInput(unsigned index, const uint8_t* bytes, size_t length, size_t layer);
void CubeStudioPointerTick(size_t layer);
void CubeStudioSensorHardware(unsigned index, uint16_t triggers);
void CubeStudioSensorInput(unsigned source, const uint8_t* bytes, size_t length, size_t layer);
bool CubeStudioSensorTick();
void CubeStudioRelayEvent(unsigned source, const std::string& name,
                          const std::vector<uint8_t>& data);
void CubeStudioRecordPositions(const cube_dongle::SplitState::Snapshot& positions);
void CubeStudioStreamPositions(const cube_dongle::SplitState::Snapshot& raw,
                               const cube_dongle::KeyboardState<61, 4>& keyboard);
void CubeStudioMacroTriggers(const cube_dongle::KeyboardState<61, 4>& keyboard);
cube_dongle::ReportFrame CubeStudioMacroOutput(cube_dongle::ReportFrame physical);
uint8_t CubeStudioCurrentModifiers(uint8_t physical_modifiers);
void CubeStudioCancelMacros();
cube_dongle::studio::ComboResult CubeStudioFilterPositions(
    const cube_dongle::SplitState::Snapshot& raw, size_t layer);
bool CubeStudioCombosNeedTick();
void CubeStudioSetActivityHandler(std::function<void(unsigned)> handler);
void CubeStudioActivityWake();
void CubeStudioPoll();
bool CubeStudioFeed(uint8_t byte);
bool CubeStudioFeedBle(uint8_t byte, uint32_t epoch);
bool CubeStudioUnlockUsb();
void CubeStudioRefreshKeymap(cube_dongle::KeyboardState<61, 4>& keyboard);
void CubeStudioLayerName(size_t layer, char* name, size_t capacity);

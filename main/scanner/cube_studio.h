#pragma once
#include <functional>
#include "../../experimental/cube_dongle/report_queue.h"
#include "../../experimental/cube_dongle/split_state.h"
void CubeStudioInitialize();
void CubeStudioRecordPositions(const cube_dongle::SplitState::Snapshot& positions);
void CubeStudioMacroTriggers(const cube_dongle::KeyboardState<61, 4>& keyboard);
cube_dongle::ReportFrame CubeStudioMacroOutput(cube_dongle::ReportFrame physical);
void CubeStudioCancelMacros();
void CubeStudioSetActivityHandler(std::function<void(unsigned)> handler);
void CubeStudioActivityWake();
void CubeStudioPoll();
bool CubeStudioFeed(uint8_t byte);
bool CubeStudioFeedBle(uint8_t byte, uint32_t epoch);
bool CubeStudioUnlockUsb();
void CubeStudioRefreshKeymap(cube_dongle::KeyboardState<61, 4>& keyboard);
void CubeStudioLayerName(size_t layer, char* name, size_t capacity);

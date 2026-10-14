#pragma once
#include "../../experimental/cube_dongle/split_state.h"
void CubeStudioInitialize();
void CubeStudioPoll();
bool CubeStudioFeed(uint8_t byte);
bool CubeStudioUnlockUsb();
void CubeStudioRefreshKeymap(cube_dongle::KeyboardState<61, 3>& keyboard);
void CubeStudioLayerName(size_t layer, char* name, size_t capacity);

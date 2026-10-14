#pragma once
#include <cstdint>
#include <string>
#include "../../experimental/cube_dongle/report_queue.h"

// Uses the split client's existing NimBLE host; never initializes a second host.
bool CubeBleHostInit();
void CubeBleHostSync(uint8_t address_type);
void CubeBleHostReset();
void CubeDualTick();
void CubeDualSubmit(const cube_dongle::ReportFrame& frame, uint16_t actions);
void CubeDualSetNeutral(bool neutral);
std::string CubeDualCommand(const std::string& line);
bool CubeDualEnabled();
bool CubeDualUsesBle();
bool CubeDualBleReady();
int CubeDualProfile();
std::string CubeDualStatus();

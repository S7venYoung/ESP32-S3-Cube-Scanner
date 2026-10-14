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
void CubeDualRequireNeutral();
std::string CubeDualCommand(const std::string& line);
uint32_t CubeDualNotice();
bool CubeDualEnabled();
bool CubeDualUsesBle();
bool CubeDualBleReady();
int CubeDualProfile();
std::string CubeDualStatus();
#if CONFIG_CUBE_STUDIO_RPC
#include "../../experimental/cube_dongle/studio_protocol.h"
cube_dongle::studio::Bytes CubeBleManagement(const cube_dongle::studio::Bytes& payload,
                                             bool unlocked);
bool CubeBleStudioReady();
uint32_t CubeBleStudioEpoch();
size_t CubeBleStudioRead(uint8_t* bytes, size_t capacity, uint32_t& epoch);
bool CubeBleStudioWrite(const uint8_t* bytes, size_t size, uint32_t epoch);
#endif

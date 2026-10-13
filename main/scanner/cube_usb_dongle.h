#pragma once
#include <cstddef>
#include <cstdint>
#include "../../experimental/cube_dongle/report_queue.h"

bool StartCubeUsbDongle();
int CubeCdcRead(char* bytes, size_t size);
void CubeCdcWrite(const char* bytes, size_t size);
void CubeUsbSubmit(const cube_dongle::ReportFrame& frame);
void CubeUsbEnable(bool enabled);
void CubeUsbRelease();

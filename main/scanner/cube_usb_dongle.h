#pragma once
#include <cstddef>
#include <cstdint>
#include "../../experimental/cube_dongle/report_queue.h"

bool StartCubeUsbDongle();
int CubeCdcRead(char* bytes, size_t size);
void CubeCdcWrite(const char* bytes, size_t size);
bool CubeCdcWritePacket(const uint8_t* bytes, size_t size);
void CubeUsbSubmit(const cube_dongle::ReportFrame& frame);
void CubeUsbEnable(bool enabled);
bool CubeUsbEnabled();
void CubeUsbRelease();
bool CubeUsbConnected();
bool CubeUsbAttached();
bool CubeUsbEnterDownload();
bool CubeUsbNeutralDelivered();
const uint8_t* CubeUsbReportMap(size_t& size);

#pragma once
#include "codex_metrics.h"
#include "fight_telemetry.h"
void StartZmkScanner();
void SetScannerVoiceActive(bool active);
void SetScannerThermalLimited(bool limited);
bool TakeScannerPriorityUpdate();

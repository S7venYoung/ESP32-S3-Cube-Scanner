# Provisioning reboot fix

Hardware UART capture of the previous Codex2 image reported repeated
`ESP_ERR_HTTPD_TASK` from `WifiConfigurationAp::StartWebServer()` and
`ESP_ERROR_CHECK` aborts, immediately after BLE initialization and Wi-Fi AP
startup. This is a task-creation failure, not a dashboard redraw problem.

## Changes

- Start BLE discovery only after `board.StartNetwork()` returns. The native
  provisioning path waits for configuration and reboot, so it cannot allocate
  BLE controller/host memory before the provisioning HTTP task.
- Allocate NimBLE dynamic host memory in PSRAM, enable only its observer role,
  and allow Wi-Fi/LwIP allocations in PSRAM.
- Lower the normal allocation internal-memory preference threshold to 1024
  bytes. Native task stacks and DMA allocations remain internal.
- Skip optional BLE discovery if internal free memory is below 64 KiB or the
  largest internal block is below 16 KiB. USB/Wi-Fi Codex sync is unchanged.
- Log internal free memory and largest block before native Wi-Fi and BLE.

The 64/16 KiB check is a conservative startup guard, not a guarantee for every
future audio/TLS workload. Discovery still provides only names/RSSI; actual
ZMK layers/WPM/battery telemetry remains unavailable.

## Hardware acceptance

Verify a fresh/unconfigured device keeps its configuration AP and HTTP page
running without abort/reboot for at least 60 seconds. Configure Wi-Fi, then
verify Codex USB/Wi-Fi updates and Xiaozhi audio. Examine internal-memory
diagnostics if BLE is skipped. Keep the existing Codex2 theme unchanged.

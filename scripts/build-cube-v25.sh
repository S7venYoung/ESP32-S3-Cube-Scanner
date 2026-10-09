#!/usr/bin/env bash
set -euo pipefail
python -m unittest discover -s scripts/tests -q
g++ -std=c++17 tests/arcade_motion_test.cc -o /tmp/arcade-motion-test
/tmp/arcade-motion-test
g++ -std=c++17 tests/arcade_assets_test.cc -o /tmp/arcade-assets-test
/tmp/arcade-assets-test
g++ -std=c++17 tests/arcade_lettering_test.cc -o /tmp/arcade-lettering-test
/tmp/arcade-lettering-test
g++ -std=c++17 tests/arcade_controls_test.cc -o /tmp/arcade-controls-test
/tmp/arcade-controls-test
g++ -std=c++17 tests/arcade_activity_test.cc -o /tmp/arcade-activity-test
/tmp/arcade-activity-test
g++ -std=c++17 tests/dashboard_command_test.cc -o /tmp/dashboard-command-test
/tmp/dashboard-command-test
g++ -std=c++17 tests/prospector_modifiers_test.cc -o /tmp/prospector-modifiers-test
/tmp/prospector-modifiers-test
g++ -std=c++17 tests/fight_telemetry_test.cc -o /tmp/fight-telemetry-test
/tmp/fight-telemetry-test
g++ -std=c++17 tests/codex_protocol_test.cc -o /tmp/codex-protocol-test
/tmp/codex-protocol-test
g++ -std=c++17 tests/codex2_drawing_test.cc -o /tmp/codex-drawing-test
/tmp/codex-drawing-test
python scripts/build.py zhengchen/1.54tft-wifi --name zhengchen-1.54tft-wifi-codex --language zh-CN --wake-word nihaoxiaozhi
grep -qx 'CONFIG_ZMK_SCANNER_MODE=y' sdkconfig
grep -qx 'CONFIG_BOARD_TYPE_ZHENGCHEN_1_54TFT_WIFI=y' sdkconfig
grep -qx 'CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y' sdkconfig
grep -qx 'CONFIG_USE_AFE_WAKE_WORD=y' sdkconfig
grep -qx 'CONFIG_ESP_CONSOLE_UART_DEFAULT=y' sdkconfig
cp sdkconfig build/sdkconfig
cp build/xiaozhi.bin build/zhengchen_154_wifi_codex.bin
cp docs/cube-codex-v2.5.md build/FLASHING.md
cd build
sha256sum merged-binary.bin zhengchen_154_wifi_codex.bin xiaozhi.bin generated_assets.bin bootloader/bootloader.bin partition_table/partition-table.bin ota_data_initial.bin > SHA256SUMS

#!/usr/bin/env bash
set -euo pipefail
python -m unittest discover -s scripts/tests -q
g++ -std=c++17 -Wall -Wextra -Werror tests/modifier_timeline_test.cc -o /tmp/modifier-timeline-test
/tmp/modifier-timeline-test
g++ -std=c++17 -Wall -Wextra -Werror tests/keyboard_theme_state_test.cc -o /tmp/keyboard-theme-state-test
/tmp/keyboard-theme-state-test
g++ -std=c++17 -Wall -Wextra -Werror tests/scanner_policy_test.cc -o /tmp/scanner-policy-test
/tmp/scanner-policy-test
g++ -std=c++17 tests/keyboard_status_test.cc -o /tmp/keyboard-status-test
/tmp/keyboard-status-test
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
python scripts/build.py nologo/xingzhi-cube-1.54tft-wifi --name xingzhi-cube-1.54tft-wifi-codex --language zh-CN --wake-word nihaoxiaozhi
grep -qx 'CONFIG_ZMK_SCANNER_MODE=y' sdkconfig
grep -qx 'CONFIG_BOARD_TYPE_XINGZHI_CUBE_1_54TFT_WIFI=y' sdkconfig
grep -qx 'CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y' sdkconfig
grep -qx 'CONFIG_USE_AFE_WAKE_WORD=y' sdkconfig
grep -qx 'CONFIG_ESP_CONSOLE_UART_DEFAULT=y' sdkconfig
cp sdkconfig build/sdkconfig
cp build/xiaozhi.bin build/xingzhi_cube_154_wifi_codex.bin
cp docs/nologo-cube-codex-v2.5.md build/FLASHING.md
cp docs/velvet46-scanner-fix.md build/SCANNER-VELVET46.md
cd build
sha256sum merged-binary.bin xingzhi_cube_154_wifi_codex.bin xiaozhi.bin generated_assets.bin bootloader/bootloader.bin partition_table/partition-table.bin ota_data_initial.bin > SHA256SUMS

#!/usr/bin/env bash
set -euo pipefail
python -m unittest discover -s scripts/tests -q
for theme_test in arcade_token_text arcade_motion fight_telemetry arcade_energy_mirror; do
    g++ -std=c++17 -Wall -Wextra -Werror "tests/${theme_test}_test.cc" -o "/tmp/cube-${theme_test}-test"
    "/tmp/cube-${theme_test}-test"
done
g++ -std=c++17 -Wall -Wextra -Werror experimental/cube_dongle/state_test.cc -o /tmp/cube-dongle-state-test
/tmp/cube-dongle-state-test
g++ -std=c++17 -Wall -Wextra -Werror experimental/cube_dongle/studio_test.cc -o /tmp/cube-studio-test
/tmp/cube-studio-test
g++ -std=c++17 -Wall -Wextra -Werror experimental/cube_dongle/studio_custom_test.cc -o /tmp/cube-studio-custom-test
/tmp/cube-studio-custom-test
g++ -std=c++17 -Wall -Wextra -Werror experimental/cube_dongle/runtime_macros_test.cc -o /tmp/cube-runtime-macros-test
/tmp/cube-runtime-macros-test
g++ -std=c++17 -Wall -Wextra -Werror experimental/cube_dongle/runtime_combos_test.cc -o /tmp/cube-runtime-combos-test
/tmp/cube-runtime-combos-test
g++ -std=c++17 -Wall -Wextra -Werror experimental/cube_dongle/hold_tap_test.cc -o /tmp/cube-hold-tap-test
/tmp/cube-hold-tap-test
for extra in split_relay peripheral_rpc dya_extended runtime_pointer runtime_sensors custom_settings host_os host_cycle system_behavior reset_time; do
    g++ -std=c++17 -Wall -Wextra -Werror "experimental/cube_dongle/${extra}_test.cc" -o "/tmp/cube-${extra}-test"
    "/tmp/cube-${extra}-test"
done
python scripts/build.py nologo/xingzhi-cube-1.54tft-wifi --name xingzhi-cube-1.54tft-wifi-dual-dongle-candidate --language zh-CN --wake-word nihaoxiaozhi
grep -qx 'CONFIG_CUBE_BLE_DONGLE_OUTPUT=y' sdkconfig
grep -qx 'CONFIG_CUBE_SPLIT_AUTO_BIND=y' sdkconfig
grep -qx 'CONFIG_CUBE_STUDIO_RPC=y' sdkconfig
grep -qx 'CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y' sdkconfig
grep -qx 'CONFIG_BT_NIMBLE_ROLE_CENTRAL=y' sdkconfig
grep -qx 'CONFIG_BT_NIMBLE_MAX_BONDS=7' sdkconfig
grep -qx 'CONFIG_TINYUSB_CDC_COUNT=1' sdkconfig
grep -qx 'CONFIG_TINYUSB_HID_COUNT=1' sdkconfig
cp sdkconfig build/sdkconfig
cp build/xiaozhi.bin build/xingzhi_cube_dual_candidate.bin
cp docs/cube-dual-candidate.md build/DUAL-CANDIDATE-NOT-FINAL.md
cp docs/cube-auto-binding.md build/AUTO-BINDING.md
cp docs/cube-dya-studio.md build/DYA-STUDIO.md
cp docs/cube-dya-stability-fix.md build/DYA-STABILITY-FIX.md
cd build
sha256sum merged-binary.bin xingzhi_cube_dual_candidate.bin xiaozhi.bin generated_assets.bin bootloader/bootloader.bin partition_table/partition-table.bin ota_data_initial.bin > SHA256SUMS

#!/usr/bin/env bash
set -euo pipefail
python -m unittest discover -s scripts/tests -q
g++ -std=c++17 -Wall -Wextra -Werror experimental/cube_dongle/state_test.cc -o /tmp/cube-dongle-state-test
/tmp/cube-dongle-state-test
python scripts/build.py nologo/xingzhi-cube-1.54tft-wifi --name xingzhi-cube-1.54tft-wifi-dongle-bench --language zh-CN --wake-word nihaoxiaozhi
grep -qx 'CONFIG_CUBE_SPLIT_BENCH=y' sdkconfig
grep -qx 'CONFIG_BT_NIMBLE_ROLE_CENTRAL=y' sdkconfig
grep -qx 'CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3' sdkconfig
grep -qx 'CONFIG_BT_NIMBLE_NVS_PERSIST=y' sdkconfig
cp sdkconfig build/sdkconfig
cp build/xiaozhi.bin build/xingzhi_cube_split_bench.bin
cp experimental/cube_dongle/README.md build/BENCH-NOT-FOR-DAILY-USE.md
cd build
sha256sum merged-binary.bin xingzhi_cube_split_bench.bin xiaozhi.bin generated_assets.bin bootloader/bootloader.bin partition_table/partition-table.bin ota_data_initial.bin > SHA256SUMS

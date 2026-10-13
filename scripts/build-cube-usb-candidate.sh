#!/usr/bin/env bash
set -euo pipefail
python -m unittest discover -s scripts/tests -q
g++ -std=c++17 -Wall -Wextra -Werror experimental/cube_dongle/state_test.cc -o /tmp/cube-dongle-state-test
/tmp/cube-dongle-state-test
python scripts/build.py nologo/xingzhi-cube-1.54tft-wifi --name xingzhi-cube-1.54tft-wifi-usb-dongle-candidate --language zh-CN --wake-word nihaoxiaozhi
grep -qx 'CONFIG_CUBE_USB_DONGLE_OUTPUT=y' sdkconfig
grep -qx 'CONFIG_TINYUSB_CDC_COUNT=1' sdkconfig
grep -qx 'CONFIG_TINYUSB_HID_COUNT=1' sdkconfig
cp sdkconfig build/sdkconfig
cp build/xiaozhi.bin build/xingzhi_cube_usb_candidate.bin
cp docs/cube-usb-candidate.md build/CANDIDATE-NOT-FINAL.md
cd build
sha256sum merged-binary.bin xingzhi_cube_usb_candidate.bin xiaozhi.bin generated_assets.bin bootloader/bootloader.bin partition_table/partition-table.bin ota_data_initial.bin > SHA256SUMS

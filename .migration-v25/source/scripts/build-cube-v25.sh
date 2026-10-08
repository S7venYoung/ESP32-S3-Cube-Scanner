#!/usr/bin/env bash
set -euo pipefail
python -m unittest discover -s scripts/tests -q
g++ -std=c++17 tests/codex_protocol_test.cc -o /tmp/codex-protocol-test
/tmp/codex-protocol-test
g++ -std=c++17 tests/codex2_drawing_test.cc -o /tmp/codex-drawing-test
/tmp/codex-drawing-test
python scripts/build.py zhengchen/1.54tft-wifi --name zhengchen-1.54tft-wifi-codex --language zh-CN --wake-word nihaoxiaozhi
cp build/xiaozhi.bin build/zhengchen_154_wifi_codex.bin
cp docs/cube-codex-v2.5.md build/FLASHING.md
cd build
sha256sum merged-binary.bin zhengchen_154_wifi_codex.bin > SHA256SUMS

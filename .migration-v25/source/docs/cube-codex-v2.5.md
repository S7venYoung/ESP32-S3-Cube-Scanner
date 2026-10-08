# Zhengchen Cube Codex / Xiaozhi v2.5.0

Hardware: official `zhengchen-1.54tft-wifi`, 240x240 ST7789, ESP32-S3.
Base: upstream v2.5.0 commit ac6deed3d8e75348475364bf40ad953c6cd48054.
Variant: `zhengchen-1.54tft-wifi-codex`; stock variant remains selectable.

The dashboard uses coverage masks and RGB565 canvas drawing, not LVGL fonts.
USB-UART and Wi-Fi use the existing CODEX2 companion protocol. Native USB must
remain disabled: GPIO20 drives the backlight. UART0 is TX43/RX44 at 115200.
Native battery (ADC1 channel7, charging GPIO9), power hold GPIO2, audio and
three buttons retain the official hardware implementation. Voice activity
appears as a temporary floating panel. Native setup and safety alerts remain.

Optional BLE discovers advertising names/RSSI only; it does not yet provide
ZMK layer, WPM or split battery telemetry. It starts after Wi-Fi connects and
can be skipped when internal memory is insufficient for native services.
Stock automatic firmware replacement is blocked by default; activation and
server configuration continue normally.

Build with ESP-IDF 6.1:

```sh
python scripts/build.py zhengchen/1.54tft-wifi --name zhengchen-1.54tft-wifi-codex --language zh-CN --wake-word nihaoxiaozhi
```

First upgrade from v1.6.2 MUST use the full `merged-binary.bin` at 0x0, since
the v2.5 official partition/voice-assets layout differs. Do not flash an app
at the old 0x100000 address. For later app-only updates use the generated
`flasher_args.json`, never a remembered address. Full flashing may reset Wi-Fi
and activation settings; back up the old flash if those must be retained.

Host tests and a successful Actions build do not establish hardware correctness.
Validate boot/no flicker, USB and Wi-Fi quotas, charging, wake/listen/speech,
buttons and reconnect on the physical unit after flashing.

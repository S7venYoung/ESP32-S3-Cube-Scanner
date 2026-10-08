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

Optional BLE discovers advertising names/RSSI and reads modifier flags from
the original Prospector 26-byte manufacturer advertisement. It does not yet
display ZMK layer, WPM or split battery telemetry. It starts after Wi-Fi connects and
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
# 麦金塔第二主题

默认仍为 Codex。说“你好小智”唤醒，然后说“切换迈克主题”或“切换麦金塔主题”；说“切换 Codex 主题”返回。选项保存，重启后保留。

长按音量加键循环切换 Codex / 麦金塔主题；短按加减音量不变。Codex 变体不再使用长按音量加键设置最大音量；音量减键原有操作保持不变。

麦金塔页面以像素绘制文字和复古窗口，保留 USB / Wi-Fi、Cube 自身电量、5 小时 / 7 天剩余额度与今日用量。键盘层、WPM、左右键盘电量尚无真实数据源，显示 `--`；不显示示例数值。小智唤醒时仍悬浮在主题上。

文字使用加粗自绘像素笔画，不调用系统字体。修饰符使用 Mac 的 ⌃ / ⌥ / ⌘ / ⇧ 自绘符号，无圆点或方框指示；按下时对应按键变为深底浅字。

修饰符来自原版 Prospector 状态广播中的 `modifier_flags`，合并左右 Control / Alt / GUI / Shift。键盘中央端需已有 `CONFIG_ZMK_STATUS_ADVERTISEMENT=y`；不占用键盘连接槽，不依赖 macOS 程序读取按键。Cube 接收首个匹配的中央端/独立键盘，60 秒无广播清除按下状态与目标绑定，避免多个键盘状态混合。可通过 `CONFIG_ZMK_SCANNER_NAME_FILTER` 限定键盘名称。外围半边广播不作为完整修饰符状态来源。

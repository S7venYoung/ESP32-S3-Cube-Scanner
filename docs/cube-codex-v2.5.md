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

Long-press volume-down cycles Codex -> Macintosh -> Arcade -> Codex; the choice
is saved across restart. Say "你好小智，切换街机主题" (or 格斗主题/街霸主题).
Short volume-down presses still reduce volume. In the Codex variant, its long
press no longer mutes, and volume-up long press has no action (damaged button).
Stock non-scanner firmware retains its original maximum-volume/mute actions.
Arcade uses the two illustrated fighters from the approved mockup, not pixel art,
and reference-derived lettering artwork rendered directly to alpha canvases,
with the original mockup's dojo reconstructed behind them (warm lamps, wood
pillars and the central 武 scroll). The fixed RGB565 stage is behind the alpha
fighters. TODAY TOKENS retains the reference's black/red brush banner, with live
values above it. Header connection and local battery numbers use the default
LVGL font; other Arcade text and smooth Mac symbols are drawn without fonts.
The gold lower frame surrounds slanted blue battery segments. WPM rows are removed from
Arcade; real left/right WPM still drives attacks internally. Every fighter has
three idle guard keyframes (neutral, inhale, exhale), on an asymmetric loop even
when no typing or no telemetry is present; no fake attacks or fake WPM values.
Codex, Macintosh and Arcade have separate hidden/shown theme layers. Switching
invalidates the full dashboard and inactive Codex widgets no longer refresh.
This prevents software layer interference; physical panel retention still needs
hardware diagnosis if ghosts persist after restart/pure-color testing.
Each fighter has guard, punch, kick,
Hadouken, Shoryuken and super-attack poses, with energy effects and recovery.
Left/right WPM independently drives attack cadence. At 70+ WPM sustained for
four seconds, the next attack becomes a super. These are dashboard animations,
not a playable fighting engine. The blue lower bars always mean keyboard battery,
not consumable attack energy. Missing battery or quota values remain --.

Optional BLE discovers advertising names/RSSI and reads modifier flags from
the original Prospector 26-byte manufacturer advertisement. Independent left/right
WPM and split batteries require the additional CUBE-FIGHT telemetry on the keyboard;
without it the fighters stay in guard and WPM/batteries display --. Layer telemetry
is not yet displayed. BLE starts after Wi-Fi connects and
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

新增独立 `ff ff ab ce 01` 格斗遥测包：Cube 接收左右 WPM、电量和修饰符，麦金塔 WPM 区显示 `左/右`。3 秒无包时速度恢复 `--/--`，不以总 WPM 代替。须配合 `zmk-sofle-dongle-dya/fighting-theme` 新版接收器广播，名称过滤若启用须允许 `CUBE-FIGHT`。原版 26 字节 Prospector 广播不变。街霸彩色角色动画主题尚未实现。

默认仍为 Codex。说“你好小智”唤醒，然后说“切换迈克主题”或“切换麦金塔主题”；说“切换 Codex 主题”返回。选项保存，重启后保留。

长按音量减键循环切换 Codex / 麦金塔 / 街机主题；短按加减音量不变。Codex 变体长按音量减不再静音，长按音量加不执行操作；原版非扫描仪固件保持最大音量 / 静音操作。

麦金塔页面以像素绘制文字和复古窗口，保留 USB / Wi-Fi、Cube 自身电量、5 小时 / 7 天剩余额度与今日用量。键盘层、WPM、左右键盘电量尚无真实数据源，显示 `--`；不显示示例数值。小智唤醒时仍悬浮在主题上。

文字使用加粗自绘像素笔画，不调用系统字体。修饰符使用 Mac 的 ⌃ / ⌥ / ⌘ / ⇧ 自绘符号，无圆点或方框指示；按下时对应按键变为深底浅字。

修饰符来自原版 Prospector 状态广播中的 `modifier_flags`，合并左右 Control / Alt / GUI / Shift。键盘中央端需已有 `CONFIG_ZMK_STATUS_ADVERTISEMENT=y`；不占用键盘连接槽，不依赖 macOS 程序读取按键。Cube 接收首个匹配的中央端/独立键盘，60 秒无广播清除按下状态与目标绑定，避免多个键盘状态混合。可通过 `CONFIG_ZMK_SCANNER_NAME_FILTER` 限定键盘名称。外围半边广播不作为完整修饰符状态来源。

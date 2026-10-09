# Nologo 星智 Cube 1.54 TFT Wi-Fi · Codex + 小智

专用于官方 v2.5.0 nologo-xingzhi-cube-1.54tft-wifi 硬件。正辰版本保留在 main 分支；本分支不改变官方星智引脚。

保留小智对话与悬浮唤醒界面，集成 Codex / 麦金塔 / 格斗主题、USB-UART 与 Wi-Fi 同步、BLE 键盘遥测、左右 WPM 驱动角色、待机和大招动画、常亮、长按音量减切主题。格斗主题默认启用（已有主题记忆保留），打字时隐藏 VS/TODAY，右侧条镜像消耗；键盘电量仅显示分段能量条。

构建：ESP-IDF v6.1，运行 bash scripts/build-nologo-cube-v25.sh，或使用 Build Nologo Xingzhi Cube Codex v2.5 工作流。

[适配及刷写说明](docs/nologo-cube-codex-v2.5.md)

首次使用完整 merged-binary.bin；应用文件为 xingzhi_cube_154_wifi_codex.bin。不要刷正辰固件，也不要使用正辰专用刷机预设。音频、电池 ADC2、USB-UART 连接和屏幕方向仍需实机验证。
